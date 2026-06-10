// nozzle - test_dmabuf_publish.cpp - C API DMA-BUF publish validation tests

#include <catch2/catch_test_macros.hpp>

#include <nozzle/config.hpp>
#include <nozzle/nozzle_c.h>

#include <string>

#if NOZZLE_HAS_DMA_BUF
#include "backends/backend_dispatch.hpp"
#include "backends/linux/linux_helpers.hpp"

#include <fcntl.h>
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
        static_cast<uint32_t>(nozzle::texture_format::rgba16_float));

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
