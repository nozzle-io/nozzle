// nozzle - test_dmabuf_publish.cpp - C API DMA-BUF publish validation tests

#include <catch2/catch_test_macros.hpp>

#include <nozzle/config.hpp>
#include <nozzle/nozzle_c.h>

#include <string>

#if NOZZLE_HAS_DMA_BUF
#include <nozzle/sender.hpp>
#include "backends/backend_dispatch.hpp"
#include "backends/linux/linux_helpers.hpp"
#include "dmabuf_dispatch_peer.hpp"

#include <atomic>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <fcntl.h>
#include <mutex>
#include <thread>
#include <unistd.h>

#include <dirent.h>
#endif

namespace {

#if NOZZLE_HAS_DMA_BUF
uint32_t invalid_fourcc() {
    return nozzle::detail::linux_backend::drm_format_from_nozzle(
        static_cast<uint32_t>(nozzle::texture_format::unknown));
}

uint32_t rgba8_fourcc() {
    return nozzle::detail::linux_backend::drm_format_from_nozzle(
        static_cast<uint32_t>(nozzle::texture_format::rgba8_unorm));
}
#endif

NozzleDmaBufPublishDesc valid_dmabuf_desc() {
    NozzleDmaBufPublishDesc desc{};
    desc.dmabuf_fd = 0;
    desc.width = 16;
    desc.height = 16;
#if NOZZLE_HAS_DMA_BUF
    desc.drm_fourcc = rgba8_fourcc();
#else
    desc.drm_fourcc = 1;
#endif
    desc.modifier = 0;
    desc.plane_count = 1;
    desc.planes[0].stride = 64;
    desc.planes[0].offset = 0;
    desc.storage_format = NOZZLE_FORMAT_RGBA8_UNORM;
    desc.semantic_format = NOZZLE_FORMAT_RGBA8_UNORM;
    desc.swizzle = NOZZLE_CHANNEL_SWIZZLE_IDENTITY;
    return desc;
}

#if NOZZLE_HAS_DMA_BUF
class unique_fd {
public:
    explicit unique_fd(int fd = -1)
    : fd_{fd}
    {}

    ~unique_fd() {
        if (0 <= fd_) {
            close(fd_);
        }
    }

    unique_fd(const unique_fd &) = delete;
    unique_fd &operator=(const unique_fd &) = delete;

    unique_fd(unique_fd &&other) noexcept
    : fd_{other.fd_}
    {
        other.fd_ = -1;
    }

    auto get() const -> int {
        return fd_;
    }

    auto release() -> int {
        int fd = fd_;
        fd_ = -1;
        return fd;
    }

private:
    int fd_{-1};
};

auto open_harmless_fd() -> unique_fd {
    return unique_fd{open("/dev/null", O_RDONLY | O_CLOEXEC)};
}

bool fd_is_open(int fd) {
    return 0 <= fd && fcntl(fd, F_GETFD) != -1;
}

auto open_fd_count() -> std::size_t {
    DIR *dir = opendir("/proc/self/fd");
    if (!dir) {
        return 0;
    }

    std::size_t count{0};
    while (readdir(dir) != nullptr) {
        count = count + 1;
    }
    closedir(dir);
    return count;
}

#if NOZZLE_ENABLE_TEST_HOOKS
struct persistent_accept_failure {
    std::atomic<uint32_t> calls{0};
    std::condition_variable first_call_cv;
    std::mutex first_call_mutex;
};

auto fail_accept_with_emfile(int, void *context) -> int {
    auto *failure = static_cast<persistent_accept_failure *>(context);
    {
        std::lock_guard<std::mutex> lock(failure->first_call_mutex);
        failure->calls.fetch_add(1, std::memory_order_relaxed);
    }
    failure->first_call_cv.notify_all();
    errno = EMFILE;
    return -1;
}
#endif

TEST_CASE("DMA-BUF backend: float nozzle formats have no DRM FourCC mapping", "[dmabuf][format]") {
    const nozzle::texture_format float_formats[] = {
        nozzle::texture_format::r16_float,
        nozzle::texture_format::rg16_float,
        nozzle::texture_format::rgb16_float,
        nozzle::texture_format::rgba16_float,
        nozzle::texture_format::r32_float,
        nozzle::texture_format::rg32_float,
        nozzle::texture_format::rgb32_float,
        nozzle::texture_format::rgba32_float,
    };

    for (auto format : float_formats) {
        CHECK(nozzle::detail::linux_backend::drm_format_from_nozzle(
            static_cast<uint32_t>(format)) == invalid_fourcc());
    }
}

TEST_CASE("DMA-BUF backend: float allocation rejects before GBM device use", "[dmabuf][format]") {
    auto result = nozzle::detail::linux_backend::allocate_dmabuf(
        nullptr,
        16,
        16,
        static_cast<uint32_t>(nozzle::texture_format::rgba16_float));

    REQUIRE(!result.ok());
    CHECK(result.error().code == nozzle::ErrorCode::UnsupportedFormat);
    CHECK(result.error().message.find("No supported DRM FourCC") != std::string::npos);
}

TEST_CASE("DMA-BUF backend: float create rejects before GBM device probe", "[dmabuf][format]") {
    auto result = nozzle::detail::linux_backend::create_dmabuf_texture(
        nullptr,
        16,
        16,
        nozzle::texture_format::rgba16_float);

    REQUIRE(!result.ok());
    CHECK(result.error().code == nozzle::ErrorCode::UnsupportedFormat);
    CHECK(result.error().message.find("No supported DRM FourCC") != std::string::npos);
}
#endif

} // anonymous namespace

TEST_CASE("C API: publish_dmabuf validates null arguments", "[c_api][dmabuf]") {
    NozzleDmaBufPublishDesc desc = valid_dmabuf_desc();

    CHECK(nozzle_sender_publish_dmabuf(nullptr, nullptr) == NOZZLE_ERROR_INVALID_ARGUMENT);
    CHECK(nozzle_sender_publish_dmabuf(nullptr, &desc) == NOZZLE_ERROR_INVALID_ARGUMENT);
}

TEST_CASE("C API: publish_dmabuf validates descriptor before backend use", "[c_api][dmabuf]") {
    NozzleDmaBufPublishDesc desc = valid_dmabuf_desc();

    SECTION("invalid fd") {
        desc.dmabuf_fd = -1;
        CHECK(nozzle_sender_publish_dmabuf(nullptr, &desc) == NOZZLE_ERROR_INVALID_ARGUMENT);
    }

    SECTION("zero width") {
        desc.width = 0;
        CHECK(nozzle_sender_publish_dmabuf(nullptr, &desc) == NOZZLE_ERROR_INVALID_ARGUMENT);
    }

    SECTION("zero height") {
        desc.height = 0;
        CHECK(nozzle_sender_publish_dmabuf(nullptr, &desc) == NOZZLE_ERROR_INVALID_ARGUMENT);
    }

    SECTION("missing fourcc") {
        desc.drm_fourcc = 0;
        CHECK(nozzle_sender_publish_dmabuf(nullptr, &desc) == NOZZLE_ERROR_INVALID_ARGUMENT);
    }

    SECTION("missing stride") {
        desc.planes[0].stride = 0;
        CHECK(nozzle_sender_publish_dmabuf(nullptr, &desc) == NOZZLE_ERROR_INVALID_ARGUMENT);
    }

    SECTION("zero planes") {
        desc.plane_count = 0;
        CHECK(nozzle_sender_publish_dmabuf(nullptr, &desc) == NOZZLE_ERROR_UNSUPPORTED_FORMAT);
    }

    SECTION("multi-plane rejected in v1") {
        desc.plane_count = 2;
        desc.planes[1].stride = 32;
        CHECK(nozzle_sender_publish_dmabuf(nullptr, &desc) == NOZZLE_ERROR_UNSUPPORTED_FORMAT);
    }

    SECTION("unknown storage format") {
        desc.storage_format = NOZZLE_FORMAT_UNKNOWN;
        CHECK(nozzle_sender_publish_dmabuf(nullptr, &desc) == NOZZLE_ERROR_UNSUPPORTED_FORMAT);
    }

    SECTION("unknown semantic format") {
        desc.semantic_format = NOZZLE_FORMAT_UNKNOWN;
        CHECK(nozzle_sender_publish_dmabuf(nullptr, &desc) == NOZZLE_ERROR_UNSUPPORTED_FORMAT);
    }

#if NOZZLE_HAS_DMA_BUF
    SECTION("fourcc must exactly match storage format") {
        desc.drm_fourcc = rgba8_fourcc() + 1;
        CHECK(nozzle_sender_publish_dmabuf(nullptr, &desc) == NOZZLE_ERROR_UNSUPPORTED_FORMAT);
    }

    SECTION("float storage format rejects old non-float fourcc mapping") {
        desc.storage_format = NOZZLE_FORMAT_RGBA16_FLOAT;
        desc.semantic_format = NOZZLE_FORMAT_RGBA16_FLOAT;
        desc.drm_fourcc = nozzle::detail::linux_backend::drm_format_from_nozzle(
            static_cast<uint32_t>(nozzle::texture_format::rgba16_unorm));
        CHECK(nozzle_sender_publish_dmabuf(nullptr, &desc) == NOZZLE_ERROR_UNSUPPORTED_FORMAT);
    }

    SECTION("float semantic format rejects non-float storage fourcc") {
        desc.storage_format = NOZZLE_FORMAT_RGBA16_UNORM;
        desc.semantic_format = NOZZLE_FORMAT_RGBA16_FLOAT;
        desc.drm_fourcc = nozzle::detail::linux_backend::drm_format_from_nozzle(
            static_cast<uint32_t>(nozzle::texture_format::rgba16_unorm));
        CHECK(nozzle_sender_publish_dmabuf(nullptr, &desc) == NOZZLE_ERROR_UNSUPPORTED_FORMAT);
    }
#endif
}

#if NOZZLE_HAS_DMA_BUF && NOZZLE_ENABLE_TEST_HOOKS

TEST_CASE("DMA-BUF fd broker bounds persistent resource retries and stops promptly", "[dmabuf][fd_broker]") {
    using namespace std::chrono_literals;

    nozzle::detail::backend::linux_sender_state state;
    persistent_accept_failure failure;
    state.test_accept_ = &fail_accept_with_emfile;
    state.test_accept_context_ = &failure;

    std::thread worker{[&state] {
        nozzle::detail::backend::run_sender_accept_loop(&state);
    }};

    bool worker_started = false;
    {
        std::unique_lock<std::mutex> lock(failure.first_call_mutex);
        worker_started = failure.first_call_cv.wait_for(lock, 2s, [&failure] {
            return 0 < failure.calls.load(std::memory_order_relaxed);
        });
    }
    if (!worker_started) {
        {
            std::lock_guard<std::mutex> lock(state.retry_mutex_);
            state.stop_flag_.store(true, std::memory_order_relaxed);
        }
        state.retry_cv_.notify_all();
        worker.join();
    }
    REQUIRE(worker_started);

    std::this_thread::sleep_for(40ms);
    const auto calls_before_stop = failure.calls.load(std::memory_order_relaxed);
    const auto stop_started = std::chrono::steady_clock::now();
    {
        std::lock_guard<std::mutex> lock(state.retry_mutex_);
        state.stop_flag_.store(true, std::memory_order_relaxed);
    }
    state.retry_cv_.notify_all();
    worker.join();
    const auto stop_elapsed = std::chrono::steady_clock::now() - stop_started;

    CHECK(calls_before_stop > 0);
    CHECK(calls_before_stop <= 8);
    CHECK(stop_elapsed < 100ms);
}

TEST_CASE("DMA-BUF fd broker classifies accept failures", "[dmabuf][fd_broker]") {
    using nozzle::detail::backend::classify_sender_accept_error;
    using nozzle::detail::backend::sender_accept_error_kind;

    CHECK(classify_sender_accept_error(EINTR) == sender_accept_error_kind::interrupted);

    const int transient_errors[] = {
        EAGAIN,
        ECONNABORTED,
        EPROTO,
        ENETDOWN,
        ENETUNREACH,
    };
    for (int error : transient_errors) {
        CHECK(classify_sender_accept_error(error) == sender_accept_error_kind::transient);
    }

    const int resource_errors[] = {EMFILE, ENFILE, ENOBUFS, ENOMEM};
    for (int error : resource_errors) {
        CHECK(classify_sender_accept_error(error) ==
            sender_accept_error_kind::resource_exhaustion);
    }

    const int fatal_errors[] = {EBADF, EINVAL, ENOTSOCK};
    for (int error : fatal_errors) {
        CHECK(classify_sender_accept_error(error) == sender_accept_error_kind::fatal);
    }
}

TEST_CASE("DMA-BUF fd broker exposes fatal accept failure", "[dmabuf][fd_broker]") {
    nozzle::detail::backend::linux_sender_state state;
    state.test_accept_ = [](int, void *) -> int {
        errno = EBADF;
        return -1;
    };

    std::thread worker{[&state] {
        nozzle::detail::backend::run_sender_accept_loop(&state);
    }};
    worker.join();

    CHECK(!state.accept_thread_running_.load(std::memory_order_acquire));
    CHECK(state.consecutive_accept_failures_.load(std::memory_order_relaxed) == 1);
    auto status = nozzle::detail::backend::sender_broker_status(&state);
    REQUIRE(!status.ok());
    CHECK(status.error().code == nozzle::ErrorCode::BackendError);
    CHECK(status.error().message.find("fatal") != std::string::npos);
    CHECK(status.error().message.find(std::to_string(EBADF)) != std::string::npos);
}

TEST_CASE("DMA-BUF fd broker clears resource diagnostics after a successful accept", "[dmabuf][fd_broker]") {
    using nozzle::detail::backend::linux_sender_state;
    using nozzle::detail::backend::run_sender_accept_loop;
    using nozzle::detail::backend::sender_accept_error_kind;
    using nozzle::detail::backend::sender_broker_status;
    linux_sender_state state;
    int sockets[2];
    REQUIRE(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
    unique_fd client{sockets[0]};
    unique_fd peer{sockets[1]};
    const uint32_t slot = 0;
    REQUIRE(send(peer.get(), &slot, sizeof(slot), 0) ==
        static_cast<ssize_t>(sizeof(slot)));

    struct accept_sequence {
        linux_sender_state *state;
        unique_fd *client;
        unsigned calls{0};
        bool saw_resource_error{false};
    } sequence{&state, &client};
    state.test_accept_context_ = &sequence;
    state.test_accept_ = [](int, void *context) -> int {
        auto &sequence = *static_cast<accept_sequence *>(context);
        sequence.calls += 1;
        if (sequence.calls == 1) {
            errno = EMFILE;
            return -1;
        }
        if (sequence.calls == 2) {
            sequence.saw_resource_error =
                sequence.state->accept_error_kind_.load(std::memory_order_acquire) ==
                    sender_accept_error_kind::resource_exhaustion &&
                sequence.state->accept_errno_.load(std::memory_order_relaxed) == EMFILE;
            return sequence.client->release();
        }
        {
            std::lock_guard<std::mutex> lock(sequence.state->retry_mutex_);
            sequence.state->stop_flag_.store(true, std::memory_order_relaxed);
        }
        sequence.state->retry_cv_.notify_all();
        errno = EINTR;
        return -1;
    };

    run_sender_accept_loop(&state);
    CHECK(sequence.calls == 3);
    CHECK(sequence.saw_resource_error);
    CHECK(state.accept_errno_.load(std::memory_order_relaxed) == 0);
    CHECK(state.accept_error_kind_.load(std::memory_order_acquire) ==
        sender_accept_error_kind::none);
    CHECK(state.consecutive_accept_failures_.load(std::memory_order_relaxed) == 0);
    CHECK(sender_broker_status(&state).ok());
}

#if !defined(NOZZLE_SHARED)
// Private broker injection must target the same linked core as the sender API.
TEST_CASE("DMA-BUF sender publication distinguishes recoverable and fatal broker state", "[dmabuf][fd_broker]") {
    using nozzle::detail::backend::cleanup_sender_socket;
    using nozzle::detail::backend::g_sender_state;
    using nozzle::detail::backend::sender_accept_error_kind;
    using nozzle::detail::backend::test_get_sender_slot_fd;
    using nozzle::detail::backend::test_sender_slot_owned_by_state;
    nozzle::sender_desc desc;
    desc.name = "fd-broker-publication-" + std::to_string(getpid());
    // No GPU allocation/import is performed: only external-fd metadata publication.
    desc.native_device = {nozzle::backend_type::dma_buf, reinterpret_cast<void *>(1), nullptr};
    desc.fallback_flags = nozzle::fallback_none;
    auto created = nozzle::sender::create(desc);
    REQUIRE(created.ok());
    auto &sender = created.value();
    // Quiesce the real listener before injecting its private status deterministically.
    cleanup_sender_socket();
    auto *state = g_sender_state.load(std::memory_order_acquire);
    REQUIRE(state != nullptr);

    SECTION("resource exhaustion does not reject external fd publication") {
        auto source = open_harmless_fd();
        REQUIRE(fd_is_open(source.get()));
        nozzle::dma_buf::publish_desc frame;
        frame.dmabuf_fd = source.get();
        frame.width = 16;
        frame.height = 16;
        frame.fourcc = rgba8_fourcc();
        frame.plane_count = 1;
        frame.planes[0].stride = 64;
        frame.storage_format = nozzle::texture_format::rgba8_unorm;
        frame.semantic_format = frame.storage_format;
        for (int error : {EMFILE, ENFILE, ENOBUFS, ENOMEM}) {
            state->accept_errno_.store(error, std::memory_order_relaxed);
            state->accept_error_kind_.store(
                sender_accept_error_kind::resource_exhaustion, std::memory_order_release);
            REQUIRE(sender.publish_dmabuf_texture(frame).ok());
        }
        CHECK(fd_is_open(test_get_sender_slot_fd(0)));
        CHECK(test_sender_slot_owned_by_state(0));
        CHECK(fd_is_open(source.get()));
    }

    SECTION("fatal broker failure rejects writable acquisition") {
        state->accept_errno_.store(EBADF, std::memory_order_relaxed);
        state->accept_error_kind_.store(sender_accept_error_kind::fatal, std::memory_order_release);
        nozzle::texture_desc frame;
        frame.width = 16;
        frame.height = 16;
        // Unknown format also prevents GPU allocation if the broker guard regresses.
        frame.format = nozzle::texture_format::unknown;
        auto result = sender.acquire_writable_frame(frame);
        REQUIRE(!result.ok());
        CHECK(result.error().code == nozzle::ErrorCode::BackendError);
        CHECK(result.error().message.find("fd broker fatal") != std::string::npos);
    }
}
#endif

TEST_CASE("DMA-BUF fd broker serves a normal client and cleans up", "[dmabuf][fd_broker]") {
    using nozzle::detail::backend::g_sender_state;

    REQUIRE(nozzle::detail::backend::test_ensure_sender_state().ok());
    nozzle::detail::backend::cleanup_sender_socket();
    nozzle::detail::backend::test_reset_sender_state();
    struct cleanup_on_exit {
        bool active{true};
        ~cleanup_on_exit() {
            if (active) {
                nozzle::detail::backend::cleanup_sender_socket();
            }
        }
    } cleanup;

    auto source_fd = open_harmless_fd();
    REQUIRE(fd_is_open(source_fd.get()));
    nozzle::detail::backend::test_set_sender_slot_fd(0, source_fd.get(), false, 1);

    static std::atomic<uint32_t> socket_sequence{0};
    const std::string uuid = "fd-broker-test-" + std::to_string(getpid()) + "-" +
        std::to_string(socket_sequence.fetch_add(1, std::memory_order_relaxed));
    REQUIRE(nozzle::detail::backend::notify_sender_uuid(uuid.c_str()).ok());

    unique_fd client{socket(AF_UNIX, SOCK_STREAM, 0)};
    REQUIRE(fd_is_open(client.get()));
    struct timeval timeout{};
    timeout.tv_sec = 2;
    REQUIRE(setsockopt(
        client.get(), SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) == 0);
    nozzle::detail::backend::abstract_socket_addr address{"nozzle_fd_" + uuid};
    REQUIRE(connect(
        client.get(),
        reinterpret_cast<struct sockaddr *>(&address.addr),
        address.len) == 0);

    const uint32_t slot = 0;
    REQUIRE(send(client.get(), &slot, sizeof(slot), 0) ==
        static_cast<ssize_t>(sizeof(slot)));
    unique_fd received_fd{nozzle::detail::linux_backend::recv_fd_response(client.get())};
    REQUIRE(fd_is_open(received_fd.get()));

    auto *state = g_sender_state.load(std::memory_order_acquire);
    REQUIRE(state != nullptr);
    nozzle::detail::backend::cleanup_sender_socket();
    cleanup.active = false;

    CHECK(!state->accept_thread_running_.load(std::memory_order_acquire));
    {
        std::lock_guard<std::mutex> lock(state->mutex_);
        CHECK(state->listen_fd_ == -1);
    }
    CHECK(nozzle::detail::backend::test_get_sender_slot_fd(0) == -1);
    CHECK(fd_is_open(source_fd.get()));
    CHECK(fd_is_open(received_fd.get()));
}

#if !defined(NOZZLE_SHARED)
// These inspect private state in the linked binary, not a separate shared core.
TEST_CASE("DMA-BUF sender returns to ring publication after direct publication", "[dmabuf][cross_tu][mixed_publish]") {
    using namespace nozzle::detail::backend;
    struct fixture {
        unsigned allocations{0};
        unsigned image_releases{0};
        test_ring_texture_hooks hooks;
        fixture() : hooks{
            [](uint32_t width, uint32_t height, nozzle::texture_format format, void *context)
                -> nozzle::Result<nozzle::texture> {
                auto &self = *static_cast<fixture *>(context);
                auto fd = open_harmless_fd();
                if (fd.get() < 0) {
                    return nozzle::Error{nozzle::ErrorCode::ResourceCreationFailed, "test fd allocation failed"};
                }
                ++self.allocations;
                // Only GBM/EGL allocation is replaced. Registration, fd ownership,
                // sender reservation/commit and destruction remain production code.
                return nozzle::detail::make_texture_from_backend(
                    context, reinterpret_cast<void *>(static_cast<intptr_t>(fd.release())),
                    width, height, format);
            },
            [](void *image, void *context) {
                if (image != context) return false;
                ++static_cast<fixture *>(context)->image_releases;
                return true;
            }, this} { active_ring_texture_hooks = &hooks; }
        ~fixture() { active_ring_texture_hooks = nullptr; }
    } gpu;

    auto external_fd = open_harmless_fd();
    REQUIRE(external_fd.get() > 0);
    {
        nozzle::sender_desc desc;
        desc.name = "mixed-publication-" + std::to_string(getpid());
        desc.ring_buffer_size = 1;
        desc.native_device = {nozzle::backend_type::dma_buf, &gpu, nullptr};
        desc.fallback_flags = nozzle::fallback_none;
        auto created = nozzle::sender::create(desc);
        REQUIRE(created.ok());
        auto &sender = created.value();
        nozzle::texture_desc texture_desc;
        texture_desc.width = 16;
        texture_desc.height = 16;
        texture_desc.format = nozzle::texture_format::rgba8_unorm;
        auto first = sender.acquire_writable_frame(texture_desc);
        REQUIRE(first.ok());
        const int ring_fd = test_get_sender_slot_fd(0);
        REQUIRE(ring_fd > 0);
        REQUIRE(sender.commit_frame(first.value()).ok());
        const auto first_generation = test_get_sender_slot_generation(0);

        nozzle::dma_buf::publish_desc direct;
        direct.dmabuf_fd = external_fd.get();
        direct.width = 16;
        direct.height = 16;
        direct.fourcc = rgba8_fourcc();
        direct.plane_count = 1;
        direct.planes[0].stride = 64;
        direct.storage_format = nozzle::texture_format::rgba8_unorm;
        direct.semantic_format = direct.storage_format;
        REQUIRE(sender.publish_dmabuf_texture(direct).ok());
        const int broker_fd = test_get_sender_slot_fd(0);
        REQUIRE(broker_fd >= 0);
        CHECK(broker_fd != external_fd.get());
        CHECK(test_sender_slot_owned_by_state(0));
        CHECK(test_get_sender_slot_generation(0) == first_generation + 1);
        CHECK(fd_is_open(external_fd.get()));

        auto next = sender.acquire_writable_frame(texture_desc);
        REQUIRE(next.ok());
        auto committed = sender.commit_frame(next.value());
        INFO(committed.ok() ? "ring commit succeeded" : committed.error().message);
        REQUIRE(committed.ok());
        CHECK(!test_sender_slot_owned_by_state(0));
        CHECK(test_get_sender_slot_generation(0) == first_generation + 2);
        CHECK(fd_is_open(test_get_sender_slot_fd(0)));
        CHECK(!fd_is_open(broker_fd));
        CHECK(fd_is_open(external_fd.get()));
    }
    CHECK(gpu.allocations == gpu.image_releases);
    CHECK(fd_is_open(external_fd.get()));
}

TEST_CASE("DMA-BUF backend: state identity crosses translation units", "[dmabuf][cross_tu]") {
    CHECK(dmabuf_dispatch_peer::sender_state_address() ==
        &nozzle::detail::backend::g_sender_state);
    CHECK(dmabuf_dispatch_peer::receiver_cache_address() ==
        &nozzle::detail::backend::get_receiver_cache());
}

TEST_CASE("DMA-BUF backend: texture destruction clears the shared slot", "[dmabuf][cross_tu]") {
    REQUIRE(nozzle::detail::backend::test_ensure_sender_state().ok());
    nozzle::detail::backend::test_reset_sender_state();
    struct reset_on_exit {
        ~reset_on_exit() { nozzle::detail::backend::test_reset_sender_state(); }
    } reset;

    auto ring_fd = open_harmless_fd();
    REQUIRE(0 < ring_fd.get());
    const int fd = ring_fd.get();
    nozzle::detail::backend::test_set_sender_slot_fd(1, fd, false, 9);
    CHECK(dmabuf_dispatch_peer::sender_slot_fd(1) == fd);

    {
        // The real destructor is compiled in texture.cpp, not this test TU.
        auto owned_texture = nozzle::detail::make_texture_from_backend(
            nullptr, reinterpret_cast<void *>(static_cast<intptr_t>(ring_fd.release())),
            16, 16, nozzle::texture_format::rgba8_unorm);
    }

    CHECK(!fd_is_open(fd));
    CHECK(nozzle::detail::backend::test_get_sender_slot_fd(1) == -1);
    CHECK(dmabuf_dispatch_peer::sender_slot_fd(1) == -1);
    CHECK(nozzle::detail::backend::test_get_sender_slot_generation(1) == 9);
}

TEST_CASE("DMA-BUF backend: reused fd numbers advance allocation generation", "[dmabuf][cross_tu]") {
    REQUIRE(nozzle::detail::backend::test_ensure_sender_state().ok());
    nozzle::detail::backend::test_reset_sender_state();
    struct reset_on_exit {
        ~reset_on_exit() { nozzle::detail::backend::test_reset_sender_state(); }
    } reset;

    auto original_fd = open_harmless_fd();
    REQUIRE(0 < original_fd.get());
    const int fd = original_fd.get();
    uint32_t old_generation;
    {
        auto original = nozzle::detail::make_texture_from_backend(
            nullptr, reinterpret_cast<void *>(static_cast<intptr_t>(original_fd.release())),
            16, 16, nozzle::texture_format::rgba8_unorm);
        // Exercise the same registration operation as create_ring_texture, without EGL.
        nozzle::detail::backend::register_ring_texture_fd(fd, 1);
        old_generation = nozzle::detail::backend::test_get_sender_slot_generation(1);
        REQUIRE(old_generation == 1);
    }
    REQUIRE(!fd_is_open(fd));

    auto replacement_fd = open_harmless_fd();
    REQUIRE(0 <= replacement_fd.get());
    if (replacement_fd.get() != fd) {
        REQUIRE(dup2(replacement_fd.get(), fd) == fd);
    } else {
        replacement_fd.release();
    }
    {
        auto replacement = nozzle::detail::make_texture_from_backend(
            nullptr, reinterpret_cast<void *>(static_cast<intptr_t>(fd)),
            16, 16, nozzle::texture_format::rgba8_unorm);
        nozzle::detail::backend::register_ring_texture_fd(fd, 1);
        CHECK(nozzle::detail::backend::test_get_sender_slot_generation(1) == old_generation + 1);
        CHECK(dmabuf_dispatch_peer::sender_slot_fd(1) == fd);
    }
    CHECK(!fd_is_open(fd));
    CHECK(dmabuf_dispatch_peer::sender_slot_fd(1) == -1);
}
#endif

TEST_CASE("DMA-BUF backend: external registration owns only duplicated fds", "[dmabuf][ownership]") {
    REQUIRE(nozzle::detail::backend::test_ensure_sender_state().ok());
    nozzle::detail::backend::test_reset_sender_state();

    auto caller_fd = open_harmless_fd();
    REQUIRE(fd_is_open(caller_fd.get()));

    auto first_resource = nozzle::detail::backend::register_external_dmabuf_for_slot(
        0, caller_fd.get(), 3);
    REQUIRE(first_resource.ok());

    int first_state_fd = nozzle::detail::backend::test_get_sender_slot_fd(0);
    REQUIRE(fd_is_open(first_state_fd));
    CHECK(first_state_fd != caller_fd.get());
    CHECK(nozzle::detail::backend::test_sender_slot_owned_by_state(0));
    CHECK(nozzle::detail::backend::test_get_sender_slot_generation(0) == 1);
    CHECK((first_resource.value() >> 32) == 1);
    CHECK(fd_is_open(caller_fd.get()));

    auto replacement_fd = open_harmless_fd();
    REQUIRE(fd_is_open(replacement_fd.get()));

    auto second_resource = nozzle::detail::backend::register_external_dmabuf_for_slot(
        0, replacement_fd.get(), 3);
    REQUIRE(second_resource.ok());

    int second_state_fd = nozzle::detail::backend::test_get_sender_slot_fd(0);
    REQUIRE(fd_is_open(second_state_fd));
    CHECK(second_state_fd != replacement_fd.get());
    CHECK(!fd_is_open(first_state_fd));
    CHECK(fd_is_open(caller_fd.get()));
    CHECK(fd_is_open(replacement_fd.get()));
    CHECK(nozzle::detail::backend::test_sender_slot_owned_by_state(0));
    CHECK(nozzle::detail::backend::test_get_sender_slot_generation(0) == 2);
    CHECK((second_resource.value() >> 32) == 2);

    nozzle::detail::backend::test_reset_sender_state();
    CHECK(!fd_is_open(second_state_fd));
    CHECK(fd_is_open(caller_fd.get()));
    CHECK(fd_is_open(replacement_fd.get()));
}

TEST_CASE("DMA-BUF backend: cleanup does not close non-owned ring fds", "[dmabuf][ownership]") {
    REQUIRE(nozzle::detail::backend::test_ensure_sender_state().ok());
    nozzle::detail::backend::test_reset_sender_state();

    auto ring_fd = open_harmless_fd();
    REQUIRE(fd_is_open(ring_fd.get()));

    nozzle::detail::backend::test_set_sender_slot_fd(
        1, ring_fd.get(), false, 9);
    CHECK(nozzle::detail::backend::test_get_sender_slot_fd(1) == ring_fd.get());
    CHECK(!nozzle::detail::backend::test_sender_slot_owned_by_state(1));

    nozzle::detail::backend::test_reset_sender_state();
    CHECK(fd_is_open(ring_fd.get()));
    CHECK(nozzle::detail::backend::test_get_sender_slot_fd(1) == -1);
}

TEST_CASE("DMA-BUF backend: receiver cache duplicates fd for returned texture ownership", "[dmabuf][cache]") {
    nozzle::detail::backend::test_reset_receiver_cache();

    auto caller_fd = open_harmless_fd();
    REQUIRE(fd_is_open(caller_fd.get()));

    int cache_owned_fd = dup(caller_fd.get());
    REQUIRE(fd_is_open(cache_owned_fd));

    uint32_t strides[4]{64, 0, 0, 0};
    uint32_t offsets[4]{0, 0, 0, 0};
    nozzle::detail::backend::test_store_receiver_cache(
        2,
        cache_owned_fd,
        "dmabuf-cache-test",
        16,
        16,
        static_cast<uint32_t>(nozzle::texture_format::rgba8_unorm),
        0,
        1,
        1,
        strides,
        offsets);

    int cached_fd = nozzle::detail::backend::test_get_receiver_cached_fd(2);
    REQUIRE(cached_fd == cache_owned_fd);
    REQUIRE(fd_is_open(cached_fd));

    auto texture_fd_result = nozzle::detail::backend::test_duplicate_receiver_cached_fd(2);
    REQUIRE(texture_fd_result.ok());
    unique_fd texture_fd{texture_fd_result.value()};
    REQUIRE(fd_is_open(texture_fd.get()));
    CHECK(texture_fd.get() != cached_fd);

    close(texture_fd.release());
    CHECK(fd_is_open(cached_fd));
    CHECK(fd_is_open(caller_fd.get()));

    nozzle::detail::backend::test_reset_receiver_cache();
    CHECK(!fd_is_open(cached_fd));
    CHECK(fd_is_open(caller_fd.get()));
}

TEST_CASE("DMA-BUF backend: lookup closes per-texture dup on EGL failure", "[dmabuf][cache]") {
    nozzle::detail::backend::test_reset_receiver_cache();

    auto caller_fd = open_harmless_fd();
    REQUIRE(fd_is_open(caller_fd.get()));

    int cache_owned_fd = dup(caller_fd.get());
    REQUIRE(fd_is_open(cache_owned_fd));

    uint32_t strides[4]{64, 0, 0, 0};
    uint32_t offsets[4]{0, 0, 0, 0};
    nozzle::detail::backend::test_store_receiver_cache(
        3,
        cache_owned_fd,
        "dmabuf-lookup-test",
        16,
        16,
        static_cast<uint32_t>(nozzle::texture_format::rgba8_unorm),
        0,
        1,
        1,
        strides,
        offsets);

    std::size_t before_count = open_fd_count();
    auto texture_result = nozzle::detail::backend::lookup_texture(
        nullptr,
        (uint64_t{1} << 32) | 3,
        16,
        16,
        static_cast<uint32_t>(nozzle::texture_format::rgba8_unorm),
        0,
        static_cast<uint32_t>(nozzle::texture_format::rgba8_unorm),
        static_cast<uint8_t>(nozzle::native_format_kind::drm_fourcc),
        rgba8_fourcc(),
        0,
        1,
        strides,
        offsets,
        0,
        "dmabuf-lookup-test");
    CHECK(!texture_result.ok());
    CHECK(open_fd_count() == before_count);
    CHECK(fd_is_open(nozzle::detail::backend::test_get_receiver_cached_fd(3)));
    CHECK(fd_is_open(caller_fd.get()));

    nozzle::detail::backend::test_reset_receiver_cache();
}

#endif
