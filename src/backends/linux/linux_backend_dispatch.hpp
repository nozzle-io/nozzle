// nozzle - linux_backend_dispatch.hpp - DMA-BUF backend dispatch implementation

#pragma once

#include "linux_helpers.hpp"
#include "linux_fd_transfer.hpp"
#include <nozzle/backends/linux.hpp>
#include <nozzle/config.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <system_error>
#include <thread>

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace nozzle {
namespace detail {
// Share state across TUs in this linked image. Embedding plugins must also keep
// core API calls local; hidden state alone does not prevent symbol interposition.
namespace backend __attribute__((visibility("hidden"))) {

#if NOZZLE_ENABLE_TEST_HOOKS
using test_peer_query_function = int (*)(int, struct ucred *, socklen_t *, void *);
struct test_ring_texture_hooks {
    Result<texture> (*create)(uint32_t, uint32_t, texture_format, void *);
    bool (*destroy_image)(void *, void *);
    void *context;
};
inline thread_local test_ring_texture_hooks *active_ring_texture_hooks = nullptr;
#endif

inline auto peer_has_effective_uid(int socket_fd, uid_t expected_uid
#if NOZZLE_ENABLE_TEST_HOOKS
    , test_peer_query_function query = nullptr, void *context = nullptr
#endif
) -> bool {
    if (expected_uid == static_cast<uid_t>(-1) || geteuid() != expected_uid) {
        return false;
    }
    struct ucred credentials{};
    socklen_t length = sizeof(credentials);
    int result;
#if NOZZLE_ENABLE_TEST_HOOKS
    if (query) {
        result = query(socket_fd, &credentials, &length, context);
    } else
#endif
    {
        result = getsockopt(socket_fd, SOL_SOCKET, SO_PEERCRED, &credentials, &length);
    }
    // PID may be zero across PID namespaces; only the effective UID is policy.
    // SO_PEERCRED is a connection-time snapshot, not continuous authentication.
    return result == 0 && length == sizeof(credentials) &&
        credentials.uid != static_cast<uid_t>(-1) &&
        credentials.uid == expected_uid && geteuid() == expected_uid;
}

enum class sender_accept_error_kind : uint8_t {
    none,
    interrupted,
    transient,
    resource_exhaustion,
    fatal,
};

struct linux_sender_state {
    const uid_t owner_uid_{geteuid()};
    std::array<int, 8> slot_fds_{};
    std::array<bool, 8> slot_fd_owned_by_state_{};
    std::array<uint32_t, 8> slot_generations_{};
    uint32_t ring_size_{0};
    int listen_fd_{-1};
    std::string socket_path_{};
    std::string uuid_{};
    std::thread accept_thread_{};
    std::atomic<bool> stop_flag_{false};
    std::atomic<int> accept_errno_{0};
    std::atomic<sender_accept_error_kind> accept_error_kind_{sender_accept_error_kind::none};
    std::atomic<uint32_t> consecutive_accept_failures_{0};
    std::atomic<bool> accept_thread_running_{false};
    std::condition_variable retry_cv_{};
    std::mutex retry_mutex_{};
    std::mutex mutex_;

#if NOZZLE_ENABLE_TEST_HOOKS
    using test_accept_function = int (*)(int, void *);
    test_accept_function test_accept_{nullptr};
    void *test_accept_context_{nullptr};
    test_peer_query_function test_peer_query_{nullptr};
    void *test_peer_query_context_{nullptr};
#endif

    linux_sender_state() {
        slot_fds_.fill(-1);
        slot_fd_owned_by_state_.fill(false);
    }
};

inline std::atomic<linux_sender_state *> g_sender_state{nullptr};

struct linux_receiver_cache {
    linux_backend::dmabuf_texture_cache cache_{};
    std::mutex mutex_;
#if NOZZLE_ENABLE_TEST_HOOKS
    test_peer_query_function test_peer_query_{nullptr};
    void *test_peer_query_context_{nullptr};
#endif
};

inline linux_receiver_cache &get_receiver_cache() {
    static linux_receiver_cache instance;
    return instance;
}

struct abstract_socket_addr {
    struct sockaddr_un addr{};
    socklen_t len{0};

    explicit abstract_socket_addr(const std::string &name) {
        addr.sun_family = AF_UNIX;
        addr.sun_path[0] = '\0';
        size_t copy_len = std::min(name.size(), sizeof(addr.sun_path) - 2);
        std::memcpy(addr.sun_path + 1, name.c_str(), copy_len);
        len = static_cast<socklen_t>(offsetof(struct sockaddr_un, sun_path) + 1 + copy_len);
    }
};

inline void handle_fd_request(int client_fd, linux_sender_state *state) {
    if (!peer_has_effective_uid(client_fd, state->owner_uid_
#if NOZZLE_ENABLE_TEST_HOOKS
        , state->test_peer_query_, state->test_peer_query_context_
#endif
    )) {
        return;
    }
    uint32_t slot_index = 0;
    ssize_t n = recv(client_fd, &slot_index, sizeof(slot_index), 0);
    if (n != sizeof(slot_index)) {
        return;
    }

    // dup() under lock so the fd remains valid even if the sender
    // destroys its ring texture (and closes the original fd) while
    // send_fd_response is still in progress outside the lock.
    int fd_to_send = -1;
    {
        std::lock_guard<std::mutex> lock(state->mutex_);
        if (slot_index < 8 && state->slot_fds_[slot_index] >= 0) {
            fd_to_send = dup(state->slot_fds_[slot_index]);
        }
    }

    if (fd_to_send >= 0) {
        // Send failure is intentionally not retried or logged here.
        // The receiver detects missing fd via recv timeout/error and
        // retries on the next acquire_frame() call.
        linux_backend::send_fd_response(client_fd, fd_to_send);
        close(fd_to_send);
    }
}

inline void close_owned_slot_fd_locked(linux_sender_state *state, uint32_t slot_index) {
    if (!state || detail::kMaxRingSlots <= slot_index) {
        return;
    }
    if (state->slot_fd_owned_by_state_[slot_index] && state->slot_fds_[slot_index] >= 0) {
        close(state->slot_fds_[slot_index]);
    }
    state->slot_fds_[slot_index] = -1;
    state->slot_fd_owned_by_state_[slot_index] = false;
}

// Keep private state types/layouts out of exported std::thread template instantiations.
inline auto accept_sender_client(linux_sender_state *state) -> int {
#if NOZZLE_ENABLE_TEST_HOOKS
    if (state->test_accept_) {
        return state->test_accept_(state->listen_fd_, state->test_accept_context_);
    }
#endif
    return accept(state->listen_fd_, nullptr, nullptr);
}

inline auto classify_sender_accept_error(int error) -> sender_accept_error_kind {
    if (error == EINTR) {
        return sender_accept_error_kind::interrupted;
    }
    if (error == EMFILE || error == ENFILE || error == ENOBUFS || error == ENOMEM) {
        return sender_accept_error_kind::resource_exhaustion;
    }
    if (error == EAGAIN || error == EWOULDBLOCK || error == ECONNABORTED ||
        error == EPROTO || error == ENETDOWN || error == ENOPROTOOPT ||
        error == EHOSTDOWN || error == ENONET || error == EHOSTUNREACH ||
        error == EOPNOTSUPP || error == ENETUNREACH) {
        return sender_accept_error_kind::transient;
    }
    return sender_accept_error_kind::fatal;
}

inline auto sender_accept_retry_delay(
    sender_accept_error_kind kind,
    uint32_t consecutive_failures
) -> std::chrono::milliseconds {
    uint32_t base_ms = 1;
    uint32_t cap_ms = 16;
    if (kind == sender_accept_error_kind::transient) {
        base_ms = 2;
        cap_ms = 64;
    } else if (kind == sender_accept_error_kind::resource_exhaustion) {
        base_ms = 10;
        cap_ms = 250;
    }

    const uint32_t exponent = std::min(
        consecutive_failures > 0 ? consecutive_failures - 1 : 0,
        uint32_t{6});
    const uint32_t delay_ms = std::min(cap_ms, base_ms << exponent);
    return std::chrono::milliseconds{delay_ms};
}

inline auto wait_for_sender_accept_retry(
    linux_sender_state *state,
    std::chrono::milliseconds delay
) -> bool {
    std::unique_lock<std::mutex> lock(state->retry_mutex_);
    return !state->retry_cv_.wait_for(lock, delay, [state] {
        return state->stop_flag_.load(std::memory_order_relaxed);
    });
}

class accepted_client_fd {
public:
    explicit accepted_client_fd(int fd)
    : fd_{fd}
    {}

    ~accepted_client_fd() {
        if (0 <= fd_) {
            close(fd_);
        }
    }

    accepted_client_fd(const accepted_client_fd &) = delete;
    accepted_client_fd &operator=(const accepted_client_fd &) = delete;

    auto get() const -> int {
        return fd_;
    }

private:
    int fd_{-1};
};

inline void run_sender_accept_loop(void *context) {
    auto *state = static_cast<linux_sender_state *>(context);
    state->accept_thread_running_.store(true, std::memory_order_release);
    uint32_t consecutive_failures = 0;
    bool resource_exhaustion_reported = false;
    while (!state->stop_flag_.load(std::memory_order_relaxed)) {
        int client_fd = accept_sender_client(state);
        if (client_fd < 0) {
            if (state->stop_flag_.load(std::memory_order_relaxed)) break;
            const int accept_error = errno;
            const auto error_kind = classify_sender_accept_error(accept_error);
            consecutive_failures = std::min(consecutive_failures + 1, uint32_t{64});
            state->accept_errno_.store(accept_error, std::memory_order_relaxed);
            state->accept_error_kind_.store(error_kind, std::memory_order_release);
            state->consecutive_accept_failures_.store(
                consecutive_failures, std::memory_order_relaxed);
            if (error_kind == sender_accept_error_kind::resource_exhaustion &&
                !resource_exhaustion_reported) {
                std::fprintf(stderr,
                    "nozzle: DMA-BUF fd broker resource exhaustion (errno %d); "
                    "retrying accepts, publication remains enabled\n", accept_error);
                resource_exhaustion_reported = true;
            }
            if (error_kind == sender_accept_error_kind::fatal) {
                break;
            }
            if (!wait_for_sender_accept_retry(
                    state,
                    sender_accept_retry_delay(error_kind, consecutive_failures))) {
                break;
            }
            continue;
        }
        accepted_client_fd client{client_fd};
        consecutive_failures = 0;
        resource_exhaustion_reported = false;
        state->accept_errno_.store(0, std::memory_order_relaxed);
        state->accept_error_kind_.store(
            sender_accept_error_kind::none, std::memory_order_release);
        state->consecutive_accept_failures_.store(0, std::memory_order_relaxed);
        struct timeval tv{};
        tv.tv_sec = 2;
        tv.tv_usec = 0;
        if (setsockopt(client.get(), SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) != 0 ||
            setsockopt(client.get(), SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) != 0) {
            continue;
        }
        handle_fd_request(client.get(), state);
    }
    state->accept_thread_running_.store(false, std::memory_order_release);
}

inline auto sender_broker_status(const linux_sender_state *state) -> Result<void> {
    if (!state) {
        return {};
    }
    const auto kind = state->accept_error_kind_.load(std::memory_order_acquire);
    if (kind != sender_accept_error_kind::fatal) {
        return {};
    }
    const int accept_error = state->accept_errno_.load(std::memory_order_relaxed);
    return Error{ErrorCode::BackendError,
        std::string{"DMA-BUF fd broker fatal accept failure (errno "} +
            std::to_string(accept_error) + ")"};
}

inline auto sender_broker_status() -> Result<void> {
    return sender_broker_status(g_sender_state.load(std::memory_order_acquire));
}

inline auto notify_sender_uuid(const char *uuid) -> Result<void> {
    if (!uuid) return {};

    auto *state = g_sender_state.load(std::memory_order_acquire);

    if (!state) {
        linux_sender_state *new_state = nullptr;
#if NOZZLE_HAS_EXCEPTIONS
        try {
#endif
            new_state = new linux_sender_state{};
#if NOZZLE_HAS_EXCEPTIONS
        } catch (const std::exception &) {
            return Error{ErrorCode::BackendError,
                "DMA-BUF sender state allocation failed"};
        }
#endif
        linux_sender_state *expected = nullptr;
        if (!g_sender_state.compare_exchange_strong(expected, new_state,
                std::memory_order_acq_rel, std::memory_order_acquire)) {
            delete new_state;
            state = expected;
        } else {
            state = new_state;
        }
    }

    if (!state) {
        return Error{ErrorCode::BackendError,
            "DMA-BUF sender state allocation failed"};
    }

    if (geteuid() != state->owner_uid_) {
        return Error{ErrorCode::BackendError,
            "DMA-BUF fd broker identity changed since initialization"};
    }

    std::string name = "nozzle_fd_";
    name += uuid;

    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        return Error{ErrorCode::BackendError,
            "DMA-BUF fd broker socket creation failed"};
    }

    abstract_socket_addr addr(name);

    if (bind(fd, reinterpret_cast<struct sockaddr *>(&addr.addr), addr.len) < 0) {
        close(fd);
        return Error{ErrorCode::BackendError,
            "DMA-BUF fd broker socket bind failed"};
    }
    if (listen(fd, 4) < 0) {
        close(fd);
        return Error{ErrorCode::BackendError,
            "DMA-BUF fd broker socket listen failed"};
    }

    {
        std::lock_guard<std::mutex> lock(state->mutex_);
        if (state->listen_fd_ >= 0) {
            close(fd);
            return Error{ErrorCode::BackendError,
                "DMA-BUF backend supports only one sender per linked core instance"};
        }
        state->listen_fd_ = fd;
        state->socket_path_ = name;
        state->uuid_ = uuid;
    }

    state->stop_flag_.store(false, std::memory_order_relaxed);
    state->accept_errno_.store(0, std::memory_order_relaxed);
    state->accept_error_kind_.store(
        sender_accept_error_kind::none, std::memory_order_release);
    state->consecutive_accept_failures_.store(0, std::memory_order_relaxed);
    state->accept_thread_running_.store(false, std::memory_order_relaxed);

#if NOZZLE_HAS_EXCEPTIONS
    try {
#endif
        state->accept_thread_ = std::thread(&run_sender_accept_loop, static_cast<void *>(state));
#if NOZZLE_HAS_EXCEPTIONS
    } catch (const std::system_error &) {
        std::lock_guard<std::mutex> lock(state->mutex_);
        close(state->listen_fd_);
        state->listen_fd_ = -1;
        state->socket_path_.clear();
        state->uuid_.clear();
        return Error{ErrorCode::BackendError,
            "DMA-BUF fd broker accept thread creation failed"};
    }
#endif

    return {};
}

inline void cleanup_sender_socket() {
    auto *state = g_sender_state.load(std::memory_order_acquire);
    if (!state) return;

    {
        // Pair the predicate update with wait_for's mutex to avoid a lost wakeup.
        std::lock_guard<std::mutex> lock(state->retry_mutex_);
        state->stop_flag_.store(true, std::memory_order_relaxed);
    }
    state->retry_cv_.notify_all();

    {
        std::lock_guard<std::mutex> lock(state->mutex_);
        if (state->listen_fd_ >= 0) {
            shutdown(state->listen_fd_, SHUT_RDWR);
        }
    }

    if (state->accept_thread_.joinable()) {
        state->accept_thread_.join();
    }

    {
        std::lock_guard<std::mutex> lock(state->mutex_);
        if (state->listen_fd_ >= 0) {
            close(state->listen_fd_);
            state->listen_fd_ = -1;
        }
        for (uint32_t i = 0; i < detail::kMaxRingSlots; ++i) {
            close_owned_slot_fd_locked(state, i);
        }
        state->socket_path_.clear();
        state->uuid_.clear();
        state->slot_fds_.fill(-1);
        state->slot_fd_owned_by_state_.fill(false);
        state->slot_generations_.fill(0);
        state->ring_size_ = 0;
    }
    state->accept_errno_.store(0, std::memory_order_relaxed);
    state->accept_error_kind_.store(
        sender_accept_error_kind::none, std::memory_order_release);
    state->consecutive_accept_failures_.store(0, std::memory_order_relaxed);
    state->accept_thread_running_.store(false, std::memory_order_relaxed);
}

inline auto get_default_device() -> void * {
    return linux_backend::get_default_gbm_device();
}

inline void register_ring_texture_fd(int fd, uint32_t ring_slot) {
    auto *state = g_sender_state.load(std::memory_order_acquire);
    if (state) {
        std::lock_guard<std::mutex> lock(state->mutex_);
        if (ring_slot < 8) {
            if (state->slot_fds_[ring_slot] != fd) {
                close_owned_slot_fd_locked(state, ring_slot);
                state->slot_generations_[ring_slot]++;
            }
            state->slot_fds_[ring_slot] = fd;
            state->slot_fd_owned_by_state_[ring_slot] = false;
            state->ring_size_ = std::max(state->ring_size_, ring_slot + 1u);
        }
    }
}

inline auto create_ring_texture(
    void *device, uint32_t width, uint32_t height, texture_format format, uint32_t ring_slot
) -> Result<texture> {
    auto tex_result =
#if NOZZLE_ENABLE_TEST_HOOKS
        active_ring_texture_hooks && active_ring_texture_hooks->create
        ? active_ring_texture_hooks->create(width, height, format, active_ring_texture_hooks->context)
        :
#endif
        linux_backend::create_dmabuf_texture(device, width, height, format);
    if (!tex_result.ok()) {
        return tex_result.error();
    }
    const auto surface = detail::get_surface_native(tex_result.value());
    register_ring_texture_fd(static_cast<int>(reinterpret_cast<intptr_t>(surface)), ring_slot);
    return tex_result;
}

// Generation encodes allocation identity: when a DMA-BUF fd changes for a slot,
// generation increments. Since DMA-BUF plane metadata (strides/offsets) is
// determined by the allocation, a generation change also implies plane metadata
// change. This makes separate plane metadata comparison in the receiver cache
// unnecessary — generation alone is sufficient to detect stale fd entries.
inline auto get_shared_resource_id(const texture &tex) -> uint64_t {
    void *surface = detail::get_surface_native(tex);
    if (!surface) {
        return detail::kInvalidSharedResourceId;
    }
    int fd = static_cast<int>(reinterpret_cast<intptr_t>(surface));

    auto *state = g_sender_state.load(std::memory_order_acquire);
    if (state) {
        std::lock_guard<std::mutex> lock(state->mutex_);
        for (uint32_t i = 0; i < detail::kMaxRingSlots; ++i) {
            if (state->slot_fds_[i] == fd) {
                return (static_cast<uint64_t>(state->slot_generations_[i]) << 32) | static_cast<uint64_t>(i);
            }
        }
    }

    return detail::kInvalidSharedResourceId;
}

inline auto register_external_dmabuf_for_slot(
    uint32_t slot,
    int dmabuf_fd,
    uint32_t ring_size
) -> Result<uint64_t> {
    if (detail::kMaxRingSlots <= slot) {
        return Error{ErrorCode::InvalidArgument, "DMA-BUF slot index out of range"};
    }
    if (dmabuf_fd < 0) {
        return Error{ErrorCode::InvalidArgument, "DMA-BUF fd is invalid"};
    }

    int owned_fd = dup(dmabuf_fd);
    if (owned_fd < 0) {
        return Error{ErrorCode::BackendError, "failed to duplicate DMA-BUF fd"};
    }

    auto *state = g_sender_state.load(std::memory_order_acquire);
    if (!state) {
        close(owned_fd);
        return Error{ErrorCode::BackendError, "DMA-BUF sender state not initialized"};
    }

    std::lock_guard<std::mutex> lock(state->mutex_);
    close_owned_slot_fd_locked(state, slot);
    state->slot_fds_[slot] = owned_fd;
    state->slot_fd_owned_by_state_[slot] = true;
    state->slot_generations_[slot]++;
    state->ring_size_ = std::max(state->ring_size_, ring_size);

    return (static_cast<uint64_t>(state->slot_generations_[slot]) << 32) |
        static_cast<uint64_t>(slot);
}

#if NOZZLE_ENABLE_TEST_HOOKS
inline auto test_ensure_sender_state() -> Result<void> {
    auto *state = g_sender_state.load(std::memory_order_acquire);
    if (state) {
        return {};
    }

    auto *new_state = new linux_sender_state{};
    linux_sender_state *expected = nullptr;
    if (!g_sender_state.compare_exchange_strong(expected, new_state,
            std::memory_order_acq_rel, std::memory_order_acquire)) {
        delete new_state;
    }
    return {};
}

inline void test_reset_sender_state() {
    auto *state = g_sender_state.load(std::memory_order_acquire);
    if (!state) {
        return;
    }

    std::lock_guard<std::mutex> lock(state->mutex_);
    if (state->listen_fd_ >= 0) {
        close(state->listen_fd_);
        state->listen_fd_ = -1;
    }
    for (uint32_t slot_index = 0; slot_index < detail::kMaxRingSlots; ++slot_index) {
        close_owned_slot_fd_locked(state, slot_index);
    }
    state->socket_path_.clear();
    state->uuid_.clear();
    state->slot_fds_.fill(-1);
    state->slot_fd_owned_by_state_.fill(false);
    state->slot_generations_.fill(0);
    state->ring_size_ = 0;
    state->accept_errno_.store(0, std::memory_order_relaxed);
    state->accept_error_kind_.store(
        sender_accept_error_kind::none, std::memory_order_release);
    state->consecutive_accept_failures_.store(0, std::memory_order_relaxed);
    state->accept_thread_running_.store(false, std::memory_order_relaxed);
}

inline void test_set_sender_slot_fd(
    uint32_t slot,
    int fd,
    bool owned_by_state,
    uint32_t generation
) {
    auto *state = g_sender_state.load(std::memory_order_acquire);
    if (!state || detail::kMaxRingSlots <= slot) {
        return;
    }

    std::lock_guard<std::mutex> lock(state->mutex_);
    close_owned_slot_fd_locked(state, slot);
    state->slot_fds_[slot] = fd;
    state->slot_fd_owned_by_state_[slot] = owned_by_state;
    state->slot_generations_[slot] = generation;
}

inline auto test_get_sender_slot_fd(uint32_t slot) -> int {
    auto *state = g_sender_state.load(std::memory_order_acquire);
    if (!state || detail::kMaxRingSlots <= slot) {
        return -1;
    }

    std::lock_guard<std::mutex> lock(state->mutex_);
    return state->slot_fds_[slot];
}

inline auto test_sender_slot_owned_by_state(uint32_t slot) -> bool {
    auto *state = g_sender_state.load(std::memory_order_acquire);
    if (!state || detail::kMaxRingSlots <= slot) {
        return false;
    }

    std::lock_guard<std::mutex> lock(state->mutex_);
    return state->slot_fd_owned_by_state_[slot];
}

inline auto test_get_sender_slot_generation(uint32_t slot) -> uint32_t {
    auto *state = g_sender_state.load(std::memory_order_acquire);
    if (!state || detail::kMaxRingSlots <= slot) {
        return 0;
    }

    std::lock_guard<std::mutex> lock(state->mutex_);
    return state->slot_generations_[slot];
}

inline void test_reset_receiver_cache() {
    auto &cache = get_receiver_cache();
    std::lock_guard<std::mutex> lock(cache.mutex_);
    cache.cache_.clear();
}

inline void test_store_receiver_cache(
    uint32_t slot,
    int fd,
    const char *sender_uuid,
    uint32_t width,
    uint32_t height,
    uint32_t format,
    uint64_t modifier,
    uint32_t generation,
    uint32_t plane_count,
    const uint32_t *plane_strides,
    const uint32_t *plane_offsets
) {
    auto &cache = get_receiver_cache();
    std::lock_guard<std::mutex> lock(cache.mutex_);
    cache.cache_.store(slot, fd, sender_uuid, width, height, format, modifier, generation,
        plane_count, plane_strides, plane_offsets);
}

inline auto test_get_receiver_cached_fd(uint32_t slot) -> int {
    auto &cache = get_receiver_cache();
    std::lock_guard<std::mutex> lock(cache.mutex_);
    return cache.cache_.get_fd(slot);
}

inline auto test_duplicate_receiver_cached_fd(uint32_t slot) -> Result<int> {
    auto &cache = get_receiver_cache();
    std::lock_guard<std::mutex> lock(cache.mutex_);
    int cached_fd = cache.cache_.get_fd(slot);
    if (cached_fd < 0) {
        return Error{ErrorCode::BackendError, "DMA-BUF cache has no fd for slot"};
    }
    int texture_fd = dup(cached_fd);
    if (texture_fd < 0) {
        return Error{ErrorCode::BackendError, "failed to duplicate cached DMA-BUF fd"};
    }
    return texture_fd;
}
#endif

inline auto get_native_surface(const texture &tex) -> void * {
    return detail::get_surface_native(tex);
}

inline void release_texture_resources(void *native_texture, void *native_surface) {
    bool image_released = false;
#if NOZZLE_ENABLE_TEST_HOOKS
    if (active_ring_texture_hooks && active_ring_texture_hooks->destroy_image) {
        image_released = active_ring_texture_hooks->destroy_image(
            native_texture, active_ring_texture_hooks->context);
    }
#endif
    if (!image_released) {
        void *egl_disp = linux_backend::get_egl_display();
        if (native_texture && egl_disp) {
            linux_backend::destroy_egl_image(egl_disp, native_texture);
        }
    }
    if (native_surface) {
        int fd = static_cast<int>(reinterpret_cast<intptr_t>(native_surface));
        if (fd >= 0) {
            auto *state = g_sender_state.load(std::memory_order_acquire);
            if (state) {
                std::lock_guard<std::mutex> lock(state->mutex_);
                for (uint32_t i = 0; i < 8; ++i) {
                    if (state->slot_fds_[i] == fd) {
                        if (!state->slot_fd_owned_by_state_[i]) {
                            state->slot_fds_[i] = -1;
                        }
                        break;
                    }
                }
            }
            linux_backend::close_drm_fd(fd);
        }
    }
}

inline auto lookup_texture(
    void * /*device*/, uint64_t shared_id, uint32_t width, uint32_t height, uint32_t format, uint8_t, uint32_t semantic_format, uint8_t native_format_kind_value, uint32_t native_format_value, uint64_t native_modifier, uint32_t native_plane_count, const uint32_t *native_plane_strides, const uint32_t *native_plane_offsets, uint64_t /*sender_pid*/, const char *sender_uuid
) -> Result<texture> {
    const uid_t receiver_uid = geteuid();
    uint32_t slot_index = static_cast<uint32_t>(shared_id & 0xFFFFFFFF);
    uint32_t generation = static_cast<uint32_t>(shared_id >> 32);
    if (slot_index >= 8) {
        return Error{ErrorCode::InvalidArgument, "DMA-BUF slot index out of range"};
    }

    // v0.1: IPC (connect/send/recv) happens under the cache mutex.
    // Timeout-protected so not a hang risk, but cache miss IPC blocks
    // concurrent lookups. Future improvement: IPC outside lock, store/get only under lock.
    auto &cache = get_receiver_cache();
    std::lock_guard<std::mutex> lock(cache.mutex_);

    if (!cache.cache_.has(slot_index, sender_uuid, width, height, format, native_modifier, generation)) {
        if (sender_uuid && sender_uuid[0] != '\0') {
            std::string socket_name = "nozzle_fd_";
            socket_name += sender_uuid;

            int sock_fd = socket(AF_UNIX, SOCK_STREAM, 0);
            if (sock_fd >= 0) {
                struct timeval tv{};
                tv.tv_sec = 2;
                tv.tv_usec = 0;
                if (setsockopt(sock_fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) == 0 &&
                    setsockopt(sock_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) == 0) {

                    abstract_socket_addr addr(socket_name);

                    if (::connect(sock_fd, reinterpret_cast<struct sockaddr *>(&addr.addr), addr.len) == 0 &&
                        peer_has_effective_uid(sock_fd, receiver_uid
#if NOZZLE_ENABLE_TEST_HOOKS
                            , cache.test_peer_query_, cache.test_peer_query_context_
#endif
                        )) {
                        ssize_t sent = ::send(sock_fd, &slot_index, sizeof(slot_index), 0);
                        if (sent == static_cast<ssize_t>(sizeof(slot_index))) {
                            int fd = linux_backend::recv_fd_response(sock_fd);
                            if (fd >= 0) {
                                cache.cache_.store(slot_index, fd, sender_uuid, width, height, format, native_modifier, generation,
                                    native_plane_count, native_plane_strides, native_plane_offsets);
                            }
                        }
                    }
                }
                close(sock_fd);
            }
        }
    }

    if (!cache.cache_.has(slot_index, sender_uuid, width, height, format, native_modifier, generation)) {
        return Error{ErrorCode::BackendError,
            "DMA-BUF slot not cached — fd transfer failed"};
    }

    int cached_fd = cache.cache_.get_fd(slot_index);
    if (cached_fd < 0) {
        return Error{ErrorCode::BackendError,
            "DMA-BUF slot cached without a valid fd"};
    }

    uint32_t fourcc = linux_backend::drm_format_from_nozzle(format);
    if (native_format_kind_value == static_cast<uint8_t>(native_format_kind::drm_fourcc) &&
        native_format_value != 0) {
        fourcc = native_format_value;
    }
    if (fourcc == 0) {
        return Error{ErrorCode::UnsupportedFormat, "DMA-BUF native fourcc is invalid"};
    }

    int texture_fd = dup(cached_fd);
    if (texture_fd < 0) {
        return Error{ErrorCode::BackendError, "failed to duplicate cached DMA-BUF fd"};
    }

    auto normalized_fd = linux_backend::normalize_owned_texture_fd(texture_fd);
    if (!normalized_fd.ok()) {
        close(texture_fd);
        return normalized_fd.error();
    }
    texture_fd = normalized_fd.value();

    void *egl_disp = linux_backend::get_egl_display();
    if (!egl_disp) {
        close(texture_fd);
        return Error{ErrorCode::BackendError, "No EGL display available"};
    }

    uint32_t pc = native_plane_count > 0 ? native_plane_count : 1;
    linux_backend::dmabuf_plane planes[4]{};
    for (uint32_t i = 0; i < pc && i < 4; ++i) {
        planes[i].stride = native_plane_strides ? native_plane_strides[i] : 0;
        planes[i].offset = native_plane_offsets ? native_plane_offsets[i] : 0;
    }

    void *egl_image = linux_backend::import_egl_image(
        egl_disp, texture_fd, width, height, fourcc, pc, planes, native_modifier);
    if (!egl_image) {
        close(texture_fd);
        return Error{ErrorCode::ResourceCreationFailed,
            "Failed to import DMA-BUF fd as EGLImage"};
    }

    void *native_surface = reinterpret_cast<void *>(static_cast<intptr_t>(texture_fd));
    native_format_desc native{};
    native.backend = backend_type::dma_buf;
    native.kind = native_format_kind::drm_fourcc;
    native.value = fourcc;
    native.modifier = native_modifier;
    native.plane_count = pc;
    for (uint32_t i = 0; i < pc && i < 4; ++i) {
        native.plane_strides[i] = planes[i].stride;
        native.plane_offsets[i] = planes[i].offset;
    }
    return make_texture_from_backend(egl_image, native_surface, width, height,
        static_cast<texture_format>(format), 0, &native,
        static_cast<texture_format>(semantic_format));
}

inline auto get_backend_type() -> backend_type {
    return backend_type::dma_buf;
}

inline auto get_native_texture(const texture &tex) -> void * {
    return get_texture_native(tex);
}

inline auto blit_textures(void * /*device*/, void * /*src*/, void * /*dst*/, uint32_t /*width*/, uint32_t /*height*/) -> Result<void> {
    return Error{ErrorCode::UnsupportedBackend, "native texture blit not implemented for DMA-BUF backend"};
}

inline auto signal_texture_ready(void * /*native_texture*/, uint32_t /*slot_index*/) -> Result<void> {
    return {};
}

inline auto wait_for_texture(void * /*native_texture*/, uint32_t /*slot_index*/, uint32_t /*timeout_ms*/) -> Result<void> {
    return {};
}

inline void release_texture_sync(void * /*native_texture*/, uint32_t /*slot_index*/) {}

inline auto validate_texture_device(void * /*device*/, void * /*native_texture*/) -> Result<void> { return {}; }

inline void release_device(void * /*device*/) {}

} // namespace backend
} // namespace detail
} // namespace nozzle
