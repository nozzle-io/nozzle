// test_metal_format_mapping.cpp — Metal format mapping round-trip tests

#if NOZZLE_HAS_METAL

#include <catch2/catch_test_macros.hpp>

#include <nozzle/types.hpp>
#include <nozzle/format_resolve.hpp>
#include "backends/metal/metal_helpers.hpp"

using namespace nozzle;

// ---------- nozzle_format_to_mtl: all formats return valid MTLPixelFormat ----------

TEST_CASE("nozzle_format_to_mtl: 8-bit unorm formats are valid", "[metal_format]") {
    REQUIRE(metal::nozzle_format_to_mtl(static_cast<uint32_t>(texture_format::r8_unorm)) != 0);
    REQUIRE(metal::nozzle_format_to_mtl(static_cast<uint32_t>(texture_format::rg8_unorm)) != 0);
    REQUIRE(metal::nozzle_format_to_mtl(static_cast<uint32_t>(texture_format::rgba8_unorm)) != 0);
    REQUIRE(metal::nozzle_format_to_mtl(static_cast<uint32_t>(texture_format::bgra8_unorm)) != 0);
}

TEST_CASE("nozzle_format_to_mtl: srgb formats are valid", "[metal_format]") {
    REQUIRE(metal::nozzle_format_to_mtl(static_cast<uint32_t>(texture_format::rgba8_srgb)) != 0);
    REQUIRE(metal::nozzle_format_to_mtl(static_cast<uint32_t>(texture_format::bgra8_srgb)) != 0);
}

TEST_CASE("nozzle_format_to_mtl: 16-bit unorm formats are valid", "[metal_format]") {
    REQUIRE(metal::nozzle_format_to_mtl(static_cast<uint32_t>(texture_format::r16_unorm)) != 0);
    REQUIRE(metal::nozzle_format_to_mtl(static_cast<uint32_t>(texture_format::rg16_unorm)) != 0);
    REQUIRE(metal::nozzle_format_to_mtl(static_cast<uint32_t>(texture_format::rgba16_unorm)) != 0);
}

TEST_CASE("nozzle_format_to_mtl: 16-bit float formats are valid", "[metal_format]") {
    REQUIRE(metal::nozzle_format_to_mtl(static_cast<uint32_t>(texture_format::r16_float)) != 0);
    REQUIRE(metal::nozzle_format_to_mtl(static_cast<uint32_t>(texture_format::rg16_float)) != 0);
    REQUIRE(metal::nozzle_format_to_mtl(static_cast<uint32_t>(texture_format::rgba16_float)) != 0);
}

TEST_CASE("nozzle_format_to_mtl: 32-bit float formats are valid", "[metal_format]") {
    REQUIRE(metal::nozzle_format_to_mtl(static_cast<uint32_t>(texture_format::r32_float)) != 0);
    REQUIRE(metal::nozzle_format_to_mtl(static_cast<uint32_t>(texture_format::rg32_float)) != 0);
    REQUIRE(metal::nozzle_format_to_mtl(static_cast<uint32_t>(texture_format::rgba32_float)) != 0);
}

TEST_CASE("nozzle_format_to_mtl: uint formats are valid", "[metal_format]") {
    REQUIRE(metal::nozzle_format_to_mtl(static_cast<uint32_t>(texture_format::r32_uint)) != 0);
    REQUIRE(metal::nozzle_format_to_mtl(static_cast<uint32_t>(texture_format::rgba32_uint)) != 0);
}

TEST_CASE("nozzle_format_to_mtl: depth32_float is valid", "[metal_format]") {
    REQUIRE(metal::nozzle_format_to_mtl(static_cast<uint32_t>(texture_format::depth32_float)) != 0);
}

TEST_CASE("nozzle_format_to_mtl: unknown returns 0 (invalid)", "[metal_format]") {
    REQUIRE(metal::nozzle_format_to_mtl(static_cast<uint32_t>(texture_format::unknown)) == 0);
}

// ---------- mtl_format_to_iosurface: all mappable formats ----------

TEST_CASE("mtl_format_to_iosurface: 8-bit unorm", "[metal_format]") {
    uint32_t pf = 0, bpe = 0;
    auto mtl = metal::nozzle_format_to_mtl(static_cast<uint32_t>(texture_format::r8_unorm));
    REQUIRE(metal::mtl_format_to_iosurface(mtl, pf, bpe));
    REQUIRE(pf == 'L008');
    REQUIRE(bpe == 1);

    mtl = metal::nozzle_format_to_mtl(static_cast<uint32_t>(texture_format::rg8_unorm));
    REQUIRE(metal::mtl_format_to_iosurface(mtl, pf, bpe));
    REQUIRE(pf == '2C08');
    REQUIRE(bpe == 2);

    mtl = metal::nozzle_format_to_mtl(static_cast<uint32_t>(texture_format::bgra8_unorm));
    REQUIRE(metal::mtl_format_to_iosurface(mtl, pf, bpe));
    REQUIRE(pf == 'BGRA');
    REQUIRE(bpe == 4);

    mtl = metal::nozzle_format_to_mtl(static_cast<uint32_t>(texture_format::rgba8_unorm));
    REQUIRE(metal::mtl_format_to_iosurface(mtl, pf, bpe));
    REQUIRE(pf == 'RGBA');
    REQUIRE(bpe == 4);
}

TEST_CASE("mtl_format_to_iosurface: srgb", "[metal_format]") {
    uint32_t pf = 0, bpe = 0;
    auto mtl = metal::nozzle_format_to_mtl(static_cast<uint32_t>(texture_format::rgba8_srgb));
    REQUIRE(metal::mtl_format_to_iosurface(mtl, pf, bpe));
    REQUIRE(pf == 'RGBA');
    REQUIRE(bpe == 4);

    mtl = metal::nozzle_format_to_mtl(static_cast<uint32_t>(texture_format::bgra8_srgb));
    REQUIRE(metal::mtl_format_to_iosurface(mtl, pf, bpe));
    REQUIRE(pf == 'BGRA');
    REQUIRE(bpe == 4);
}

TEST_CASE("mtl_format_to_iosurface: 16-bit unorm", "[metal_format]") {
    uint32_t pf = 0, bpe = 0;
    auto mtl = metal::nozzle_format_to_mtl(static_cast<uint32_t>(texture_format::r16_unorm));
    REQUIRE(metal::mtl_format_to_iosurface(mtl, pf, bpe));
    REQUIRE(pf == 'L016');
    REQUIRE(bpe == 2);

    mtl = metal::nozzle_format_to_mtl(static_cast<uint32_t>(texture_format::rg16_unorm));
    REQUIRE(metal::mtl_format_to_iosurface(mtl, pf, bpe));
    REQUIRE(pf == '2C16');
    REQUIRE(bpe == 4);

    // rgba16_unorm: no IOSurface FourCC mapping (pre-existing gap)
}

TEST_CASE("mtl_format_to_iosurface: 16-bit float", "[metal_format]") {
    uint32_t pf = 0, bpe = 0;
    auto mtl = metal::nozzle_format_to_mtl(static_cast<uint32_t>(texture_format::r16_float));
    REQUIRE(metal::mtl_format_to_iosurface(mtl, pf, bpe));
    REQUIRE(pf == 'L00h');
    REQUIRE(bpe == 2);

    mtl = metal::nozzle_format_to_mtl(static_cast<uint32_t>(texture_format::rg16_float));
    REQUIRE(metal::mtl_format_to_iosurface(mtl, pf, bpe));
    REQUIRE(pf == '2C0h');
    REQUIRE(bpe == 4);

    mtl = metal::nozzle_format_to_mtl(static_cast<uint32_t>(texture_format::rgba16_float));
    REQUIRE(metal::mtl_format_to_iosurface(mtl, pf, bpe));
    REQUIRE(pf == 'RGhA');
    REQUIRE(bpe == 8);
}

TEST_CASE("mtl_format_to_iosurface: 32-bit float", "[metal_format]") {
    uint32_t pf = 0, bpe = 0;
    auto mtl = metal::nozzle_format_to_mtl(static_cast<uint32_t>(texture_format::r32_float));
    REQUIRE(metal::mtl_format_to_iosurface(mtl, pf, bpe));
    REQUIRE(pf == 'L00f');
    REQUIRE(bpe == 4);

    mtl = metal::nozzle_format_to_mtl(static_cast<uint32_t>(texture_format::rg32_float));
    REQUIRE(metal::mtl_format_to_iosurface(mtl, pf, bpe));
    REQUIRE(pf == '2C0f');
    REQUIRE(bpe == 8);

    mtl = metal::nozzle_format_to_mtl(static_cast<uint32_t>(texture_format::rgba32_float));
    REQUIRE(metal::mtl_format_to_iosurface(mtl, pf, bpe));
    REQUIRE(pf == 'RGfA');
    REQUIRE(bpe == 16);
}

TEST_CASE("mtl_format_to_iosurface: uint formats", "[metal_format]") {
    uint32_t pf = 0, bpe = 0;
    auto mtl = metal::nozzle_format_to_mtl(static_cast<uint32_t>(texture_format::r32_uint));
    REQUIRE(metal::mtl_format_to_iosurface(mtl, pf, bpe));
    REQUIRE(pf == 'L00f');
    REQUIRE(bpe == 4);

    mtl = metal::nozzle_format_to_mtl(static_cast<uint32_t>(texture_format::rgba32_uint));
    REQUIRE(metal::mtl_format_to_iosurface(mtl, pf, bpe));
    REQUIRE(pf == 'RGfA');
    REQUIRE(bpe == 16);
}

// ---------- uint vs float share same IOSurface FourCC ----------

TEST_CASE("uint and float 32-bit share same IOSurface FourCC", "[metal_format]") {
    uint32_t pf_uint = 0, pf_float = 0, bpe = 0;

    auto mtl_r32u = metal::nozzle_format_to_mtl(static_cast<uint32_t>(texture_format::r32_uint));
    auto mtl_r32f = metal::nozzle_format_to_mtl(static_cast<uint32_t>(texture_format::r32_float));
    REQUIRE(mtl_r32u != mtl_r32f); // different MTLPixelFormat
    REQUIRE(metal::mtl_format_to_iosurface(mtl_r32u, pf_uint, bpe));
    REQUIRE(metal::mtl_format_to_iosurface(mtl_r32f, pf_float, bpe));
    REQUIRE(pf_uint == pf_float); // same IOSurface FourCC

    auto mtl_rgba32u = metal::nozzle_format_to_mtl(static_cast<uint32_t>(texture_format::rgba32_uint));
    auto mtl_rgba32f = metal::nozzle_format_to_mtl(static_cast<uint32_t>(texture_format::rgba32_float));
    REQUIRE(mtl_rgba32u != mtl_rgba32f);
    REQUIRE(metal::mtl_format_to_iosurface(mtl_rgba32u, pf_uint, bpe));
    REQUIRE(metal::mtl_format_to_iosurface(mtl_rgba32f, pf_float, bpe));
    REQUIRE(pf_uint == pf_float);
}

// ---------- bytes_per_element consistency with resolve_bytes_per_pixel ----------

TEST_CASE("mtl_format_to_iosurface: bpe matches resolve_bytes_per_pixel for all formats", "[metal_format]") {
    const texture_format formats[] = {
        texture_format::r8_unorm,
        texture_format::rg8_unorm,
        texture_format::rgba8_unorm,
        texture_format::bgra8_unorm,
        texture_format::r16_unorm,
        texture_format::rg16_unorm,
        texture_format::r16_float,
        texture_format::rg16_float,
        texture_format::rgba16_float,
        texture_format::r32_float,
        texture_format::rg32_float,
        texture_format::rgba32_float,
        texture_format::r32_uint,
        texture_format::rgba32_uint,
    };

    for (auto fmt : formats) {
        auto mtl = metal::nozzle_format_to_mtl(static_cast<uint32_t>(fmt));
        REQUIRE(mtl != 0);

        uint32_t pf = 0, bpe = 0;
        REQUIRE(metal::mtl_format_to_iosurface(mtl, pf, bpe));
        REQUIRE(bpe == resolve_bytes_per_pixel(fmt));
    }
}

// ---------- depth and unknown have no IOSurface mapping ----------

TEST_CASE("mtl_format_to_iosurface: depth32_float has no IOSurface mapping", "[metal_format]") {
    auto mtl = metal::nozzle_format_to_mtl(static_cast<uint32_t>(texture_format::depth32_float));
    REQUIRE(mtl != 0);

    uint32_t pf = 0, bpe = 0;
    REQUIRE_FALSE(metal::mtl_format_to_iosurface(mtl, pf, bpe));
}

TEST_CASE("mtl_format_to_iosurface: rgba16_unorm has no IOSurface mapping", "[metal_format]") {
    auto mtl = metal::nozzle_format_to_mtl(static_cast<uint32_t>(texture_format::rgba16_unorm));
    REQUIRE(mtl != 0);

    uint32_t pf = 0, bpe = 0;
    REQUIRE_FALSE(metal::mtl_format_to_iosurface(mtl, pf, bpe));
}

TEST_CASE("mtl_format_to_iosurface: invalid MTLPixelFormat returns false", "[metal_format]") {
    uint32_t pf = 0, bpe = 0;
    REQUIRE_FALSE(metal::mtl_format_to_iosurface(0, pf, bpe)); // MTLPixelFormatInvalid
}

#endif // NOZZLE_HAS_METAL
