#include <catch2/catch_test_macros.hpp>

#include <nozzle/nozzle_c.h>

#include <cstdint>

TEST_CASE("C API: backend capability query rejects invalid arguments", "[c_api][backend_capabilities]") {
    CHECK(nozzle_get_backend_capabilities(NOZZLE_BACKEND_D3D11, nullptr)
        == NOZZLE_ERROR_INVALID_ARGUMENT);

    NozzleBackendCapabilities caps{};
    CHECK(nozzle_get_backend_capabilities(NOZZLE_BACKEND_UNKNOWN, &caps)
        == NOZZLE_ERROR_UNSUPPORTED_BACKEND);
    CHECK(caps.struct_size == 0);
    CHECK(caps.version == 0);

    caps = {};
    caps.struct_size = sizeof(NozzleBackendCapabilities) - 1;
    CHECK(nozzle_get_backend_capabilities(NOZZLE_BACKEND_D3D11, &caps)
        == NOZZLE_ERROR_INVALID_ARGUMENT);

    caps = {};
    caps.struct_size = sizeof(NozzleBackendCapabilities) + 8;
    CHECK(nozzle_get_backend_capabilities(NOZZLE_BACKEND_D3D11, &caps)
        == NOZZLE_ERROR_INVALID_ARGUMENT);
}

TEST_CASE("C API: valid backend capability query returns ABI metadata", "[c_api][backend_capabilities]") {
    NozzleBackendCapabilities caps{};
    REQUIRE(nozzle_get_backend_capabilities(NOZZLE_BACKEND_D3D11, &caps) == NOZZLE_OK);

    CHECK(caps.struct_size == sizeof(NozzleBackendCapabilities));
    CHECK(caps.version == NOZZLE_BACKEND_CAPABILITIES_VERSION);
    CHECK(caps.backend == NOZZLE_BACKEND_D3D11);
    CHECK(caps.native_format_kind == NOZZLE_NATIVE_KIND_DXGI_FORMAT);
    CHECK((caps.sharing_mechanisms & NOZZLE_SHARING_D3D11_NT_HANDLE) != 0);
    CHECK((caps.capability_flags & NOZZLE_BACKEND_CAP_SENDER) != 0);
}

TEST_CASE("C API: backend capability format helper validates struct and format range", "[c_api][backend_capabilities]") {
    NozzleBackendCapabilities caps{};
    REQUIRE(nozzle_get_backend_capabilities(NOZZLE_BACKEND_D3D11, &caps) == NOZZLE_OK);

    CHECK(nozzle_backend_capabilities_support_format(
        &caps,
        NOZZLE_FORMAT_RGBA8_UNORM,
        caps.writable_storage_format_bits) == 1);
    CHECK(nozzle_backend_capabilities_support_format(
        &caps,
        NOZZLE_FORMAT_UNKNOWN,
        caps.writable_storage_format_bits) == 0);
    CHECK(nozzle_backend_capabilities_support_format(
        &caps,
        static_cast<NozzleTextureFormat>(64),
        caps.writable_storage_format_bits) == 0);

    auto too_small = caps;
    too_small.struct_size = sizeof(NozzleBackendCapabilities) - 1;
    CHECK(nozzle_backend_capabilities_support_format(
        &too_small,
        NOZZLE_FORMAT_RGBA8_UNORM,
        too_small.writable_storage_format_bits) == 0);

    auto too_large = caps;
    too_large.struct_size = sizeof(NozzleBackendCapabilities) + 8;
    CHECK(nozzle_backend_capabilities_support_format(
        &too_large,
        NOZZLE_FORMAT_RGBA8_UNORM,
        too_large.writable_storage_format_bits) == 0);

    auto wrong_version = caps;
    wrong_version.version = NOZZLE_BACKEND_CAPABILITIES_VERSION + 1;
    CHECK(nozzle_backend_capabilities_support_format(
        &wrong_version,
        NOZZLE_FORMAT_RGBA8_UNORM,
        wrong_version.writable_storage_format_bits) == 0);

    CHECK(nozzle_backend_capabilities_support_format(
        nullptr,
        NOZZLE_FORMAT_RGBA8_UNORM,
        caps.writable_storage_format_bits) == 0);
}

TEST_CASE("C API: RGB semantic is not reported as D3D11 direct storage", "[c_api][backend_capabilities]") {
    NozzleBackendCapabilities caps{};
    REQUIRE(nozzle_get_backend_capabilities(NOZZLE_BACKEND_D3D11, &caps) == NOZZLE_OK);

    CHECK(nozzle_backend_capabilities_support_format(
        &caps,
        NOZZLE_FORMAT_RGB8_UNORM,
        caps.requested_format_bits) == 1);
    CHECK(nozzle_backend_capabilities_support_format(
        &caps,
        NOZZLE_FORMAT_RGB8_UNORM,
        caps.writable_storage_format_bits) == 0);
    CHECK(nozzle_backend_capabilities_support_format(
        &caps,
        NOZZLE_FORMAT_RGBA8_UNORM,
        caps.writable_storage_format_bits) == 1);
}

TEST_CASE("C API: backend availability reports current compiled library", "[c_api][backend_capabilities]") {
#if NOZZLE_HAS_METAL
    CHECK(nozzle_backend_is_available(NOZZLE_BACKEND_METAL) == 1);
#else
    CHECK(nozzle_backend_is_available(NOZZLE_BACKEND_METAL) == 0);
#endif

#if NOZZLE_HAS_D3D11
    CHECK(nozzle_backend_is_available(NOZZLE_BACKEND_D3D11) == 1);
#else
    CHECK(nozzle_backend_is_available(NOZZLE_BACKEND_D3D11) == 0);
#endif

#if NOZZLE_HAS_DMA_BUF
    CHECK(nozzle_backend_is_available(NOZZLE_BACKEND_DMA_BUF) == 1);
#else
    CHECK(nozzle_backend_is_available(NOZZLE_BACKEND_DMA_BUF) == 0);
#endif

#if NOZZLE_HAS_OPENGL
    CHECK(nozzle_backend_is_available(NOZZLE_BACKEND_OPENGL) == 1);
#else
    CHECK(nozzle_backend_is_available(NOZZLE_BACKEND_OPENGL) == 0);
#endif

    CHECK(nozzle_backend_is_available(NOZZLE_BACKEND_UNKNOWN) == 0);
}

TEST_CASE("C API: Metal capability excludes sRGB IOSurface paths rejected by backend", "[c_api][backend_capabilities]") {
    NozzleBackendCapabilities caps{};
    REQUIRE(nozzle_get_backend_capabilities(NOZZLE_BACKEND_METAL, &caps) == NOZZLE_OK);

    CHECK(nozzle_backend_capabilities_support_format(
        &caps,
        NOZZLE_FORMAT_RGBA8_SRGB,
        caps.requested_format_bits) == 0);
    CHECK(nozzle_backend_capabilities_support_format(
        &caps,
        NOZZLE_FORMAT_BGRA8_SRGB,
        caps.requested_format_bits) == 0);
    CHECK(nozzle_backend_capabilities_support_format(
        &caps,
        NOZZLE_FORMAT_RGBA8_SRGB,
        caps.writable_storage_format_bits) == 0);
    CHECK(nozzle_backend_capabilities_support_format(
        &caps,
        NOZZLE_FORMAT_BGRA8_SRGB,
        caps.native_publish_format_bits) == 0);
    CHECK(nozzle_backend_capabilities_support_format(
        &caps,
        NOZZLE_FORMAT_RGBA8_SRGB,
        caps.direct_publish_format_bits) == 0);
}

TEST_CASE("C API: OpenGL capability excludes sRGB publish rejected on macOS IOSurface path", "[c_api][backend_capabilities]") {
    NozzleBackendCapabilities caps{};
    REQUIRE(nozzle_get_backend_capabilities(NOZZLE_BACKEND_OPENGL, &caps) == NOZZLE_OK);

    CHECK(nozzle_backend_capabilities_support_format(
        &caps,
        NOZZLE_FORMAT_RGBA8_SRGB,
        caps.native_publish_format_bits) == 0);
    CHECK(nozzle_backend_capabilities_support_format(
        &caps,
        NOZZLE_FORMAT_BGRA8_SRGB,
        caps.native_publish_format_bits) == 0);
}

#if NOZZLE_HAS_D3D11
TEST_CASE("C API: current platform D3D11 capability has operation format bits", "[c_api][backend_capabilities]") {
    NozzleBackendCapabilities caps{};
    REQUIRE(nozzle_get_backend_capabilities(NOZZLE_BACKEND_D3D11, &caps) == NOZZLE_OK);
    CHECK(nozzle_backend_capabilities_support_format(
        &caps,
        NOZZLE_FORMAT_RGBA8_UNORM,
        caps.native_publish_format_bits) == 1);
}
#endif

#if NOZZLE_HAS_METAL
TEST_CASE("C API: current platform Metal capability has operation format bits", "[c_api][backend_capabilities]") {
    NozzleBackendCapabilities caps{};
    REQUIRE(nozzle_get_backend_capabilities(NOZZLE_BACKEND_METAL, &caps) == NOZZLE_OK);
    CHECK(nozzle_backend_capabilities_support_format(
        &caps,
        NOZZLE_FORMAT_RGBA8_UNORM,
        caps.direct_publish_format_bits) == 1);
}
#endif

#if NOZZLE_HAS_DMA_BUF
TEST_CASE("C API: current platform DMA-BUF capability rejects float formats", "[c_api][backend_capabilities]") {
    NozzleBackendCapabilities caps{};
    REQUIRE(nozzle_get_backend_capabilities(NOZZLE_BACKEND_DMA_BUF, &caps) == NOZZLE_OK);
    CHECK(nozzle_backend_capabilities_support_format(
        &caps,
        NOZZLE_FORMAT_RGBA8_UNORM,
        caps.requested_format_bits) == 1);
    CHECK(nozzle_backend_capabilities_support_format(
        &caps,
        NOZZLE_FORMAT_RGBA8_UNORM,
        caps.direct_publish_format_bits) == 1);

    const NozzleTextureFormat float_formats[] = {
        NOZZLE_FORMAT_R16_FLOAT,
        NOZZLE_FORMAT_RG16_FLOAT,
        NOZZLE_FORMAT_RGB16_FLOAT,
        NOZZLE_FORMAT_RGBA16_FLOAT,
        NOZZLE_FORMAT_R32_FLOAT,
        NOZZLE_FORMAT_RG32_FLOAT,
        NOZZLE_FORMAT_RGB32_FLOAT,
        NOZZLE_FORMAT_RGBA32_FLOAT,
    };

    for (auto format : float_formats) {
        CHECK(nozzle_backend_capabilities_support_format(
            &caps,
            format,
            caps.requested_format_bits) == 0);
        CHECK(nozzle_backend_capabilities_support_format(
            &caps,
            format,
            caps.writable_storage_format_bits) == 0);
        CHECK(nozzle_backend_capabilities_support_format(
            &caps,
            format,
            caps.direct_publish_format_bits) == 0);
        CHECK(nozzle_backend_capabilities_support_format(
            &caps,
            format,
            caps.cpu_read_format_bits) == 0);
        CHECK(nozzle_backend_capabilities_support_format(
            &caps,
            format,
            caps.cpu_write_format_bits) == 0);
        CHECK(nozzle_backend_capabilities_support_format(
            &caps,
            format,
            caps.known_quality_loss_format_bits) == 0);
    }
}
#endif
