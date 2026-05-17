// nozzle - linux_backend_dispatch.hpp - DMA-BUF backend dispatch implementation

#pragma once

#include "linux_helpers.hpp"
#include "linux_fd_transfer.hpp"
#include <nozzle/backends/linux.hpp>
#include <nozzle/config.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
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
namespace backend {

namespace {

struct linux_sender_state {
    std::array<int, 8> slot_fds_{};
    std::array<uint32_t, 8> slot_generations_{};
    uint32_t ring_size_{0};
    int listen_fd_{-1};
    std::string socket_path_{};
    std::string uuid_{};
    std::thread accept_thread_{};
    std::atomic<bool> stop_flag_{false};
    std::mutex mutex_;

    linux_sender_state() {
        slot_fds_.fill(-1);
    }
};

std::atomic<linux_sender_state *> g_sender_state{nullptr};

struct linux_receiver_cache {
    linux_backend::dmabuf_texture_cache cache_{};
    std::mutex mutex_;
};

linux_receiver_cache &get_receiver_cache() {
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

} // anonymous namespace

inline auto notify_sender_uuid(const char *uuid) -> Result<void> {
    if (!uuid) return {};

    auto *state = g_sender_state.load(std::memory_order_relaxed);

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
                std::memory_order_release, std::memory_order_relaxed)) {
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
                "DMA-BUF backend supports only one sender per process"};
        }
        state->listen_fd_ = fd;
        state->socket_path_ = name;
        state->uuid_ = uuid;
    }

    state->stop_flag_.store(false, std::memory_order_relaxed);

#if NOZZLE_HAS_EXCEPTIONS
    try {
#endif
        state->accept_thread_ = std::thread([state]() {
            while (!state->stop_flag_.load(std::memory_order_relaxed)) {
                int client_fd = accept(state->listen_fd_, nullptr, nullptr);
                if (client_fd < 0) {
                    if (state->stop_flag_.load(std::memory_order_relaxed)) break;
                    continue;
                }
                struct timeval tv{};
                tv.tv_sec = 2;
                tv.tv_usec = 0;
                if (setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) != 0 ||
                    setsockopt(client_fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) != 0) {
                    close(client_fd);
                    continue;
                }
                handle_fd_request(client_fd, state);
                close(client_fd);
            }
        });
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
    auto *state = g_sender_state.load(std::memory_order_relaxed);
    if (!state) return;

    state->stop_flag_.store(true, std::memory_order_relaxed);

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
        state->socket_path_.clear();
        state->uuid_.clear();
        state->slot_fds_.fill(-1);
        state->slot_generations_.fill(0);
        state->ring_size_ = 0;
    }
}

inline auto get_default_device() -> void * {
    return linux_backend::get_default_gbm_device();
}

inline auto create_ring_texture(
    void *device, uint32_t width, uint32_t height, uint32_t format, uint32_t ring_slot
) -> Result<texture> {
    auto tex_result = linux_backend::create_dmabuf_texture(device, width, height, format);
    if (!tex_result.ok()) {
        return tex_result.error();
    }

    auto *state = g_sender_state.load(std::memory_order_relaxed);
    if (state) {
        std::lock_guard<std::mutex> lock(state->mutex_);
        void *surface = detail::get_surface_native(tex_result.value());
        int fd = static_cast<int>(reinterpret_cast<intptr_t>(surface));

        if (ring_slot < 8) {
            if (state->slot_fds_[ring_slot] != fd) {
                state->slot_generations_[ring_slot]++;
            }
            state->slot_fds_[ring_slot] = fd;
            state->ring_size_ = std::max(state->ring_size_, ring_slot + 1u);
        }
    }

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

    auto *state = g_sender_state.load(std::memory_order_relaxed);
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

inline auto get_native_surface(const texture &tex) -> void * {
    return detail::get_surface_native(tex);
}

inline void release_texture_resources(void *native_texture, void *native_surface) {
    void *egl_disp = linux_backend::get_egl_display();
    if (native_texture && egl_disp) {
        linux_backend::destroy_egl_image(egl_disp, native_texture);
    }
    if (native_surface) {
        int fd = static_cast<int>(reinterpret_cast<intptr_t>(native_surface));
        if (fd >= 0) {
            auto *state = g_sender_state.load(std::memory_order_relaxed);
            if (state) {
                std::lock_guard<std::mutex> lock(state->mutex_);
                for (uint32_t i = 0; i < 8; ++i) {
                    if (state->slot_fds_[i] == fd) {
                        state->slot_fds_[i] = -1;
                        break;
                    }
                }
            }
            linux_backend::close_drm_fd(fd);
        }
    }
}

inline auto lookup_texture(
    void * /*device*/, uint64_t shared_id, uint32_t width, uint32_t height, uint32_t format, uint8_t, uint32_t semantic_format, uint64_t native_modifier, uint32_t native_plane_count, const uint32_t *native_plane_strides, const uint32_t *native_plane_offsets, const char *sender_uuid
) -> Result<texture> {
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

                    if (::connect(sock_fd, reinterpret_cast<struct sockaddr *>(&addr.addr), addr.len) == 0) {
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

    int fd = cache.cache_.get_fd(slot_index);
    uint32_t fourcc = linux_backend::drm_format_from_nozzle(format);
    void *egl_disp = linux_backend::get_egl_display();
    if (!egl_disp) {
        return Error{ErrorCode::BackendError, "No EGL display available"};
    }

    uint32_t pc = native_plane_count > 0 ? native_plane_count : 1;
    linux_backend::dmabuf_plane planes[4]{};
    for (uint32_t i = 0; i < pc && i < 4; ++i) {
        planes[i].stride = native_plane_strides ? native_plane_strides[i] : 0;
        planes[i].offset = native_plane_offsets ? native_plane_offsets[i] : 0;
    }

    void *egl_image = linux_backend::import_egl_image(
        egl_disp, fd, width, height, fourcc, pc, planes, native_modifier);
    if (!egl_image) {
        return Error{ErrorCode::ResourceCreationFailed,
            "Failed to import DMA-BUF fd as EGLImage"};
    }

    void *native_surface = reinterpret_cast<void *>(static_cast<intptr_t>(fd));
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
    return make_texture_from_backend(egl_image, native_surface, width, height, format, 0, &native, semantic_format);
}

inline auto wrap_backend_texture(
    void *backend_texture, void *backend_surface,
    uint32_t width, uint32_t height, uint32_t pixel_format
) -> texture {
    uint32_t fourcc = linux_backend::drm_format_from_nozzle(pixel_format);
    native_format_desc native{};
    native.backend = backend_type::dma_buf;
    native.kind = native_format_kind::drm_fourcc;
    native.value = fourcc;
    return make_texture_from_backend(backend_texture, backend_surface, width, height, pixel_format, 0, &native);
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

inline void release_device(void * /*device*/) {}

} // namespace backend
} // namespace detail
} // namespace nozzle
