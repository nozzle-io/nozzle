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

    mtl = metal::nozzle_format_to_mtl(static_cast<uint32_t>(texture_format::rgba16_unorm));
    REQUIRE(metal::mtl_format_to_iosurface(mtl, pf, bpe));
    REQUIRE(pf == 'RGhA');
    REQUIRE(bpe == 8);
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

TEST_CASE("unorm and float 16-bit RGBA share same IOSurface FourCC", "[metal_format]") {
    uint32_t pf_unorm = 0, pf_float = 0, bpe = 0;

    auto mtl_unorm = metal::nozzle_format_to_mtl(static_cast<uint32_t>(texture_format::rgba16_unorm));
    auto mtl_float = metal::nozzle_format_to_mtl(static_cast<uint32_t>(texture_format::rgba16_float));
    REQUIRE(mtl_unorm != mtl_float);
    REQUIRE(metal::mtl_format_to_iosurface(mtl_unorm, pf_unorm, bpe));
    REQUIRE(metal::mtl_format_to_iosurface(mtl_float, pf_float, bpe));
    REQUIRE(pf_unorm == pf_float);
}

TEST_CASE("Metal IOSurface descriptor keeps alias storage formats typed", "[metal_format]") {
    uint32_t storage = 0;
    uint32_t pf_float = 0, pf_uint = 0, bpe = 0;

    auto rgba32f = metal::nozzle_format_to_mtl(static_cast<uint32_t>(texture_format::rgba32_float));
    auto rgba32u = metal::nozzle_format_to_mtl(static_cast<uint32_t>(texture_format::rgba32_uint));
    REQUIRE(rgba32f != rgba32u);
    REQUIRE(metal::mtl_format_to_iosurface(rgba32f, pf_float, bpe));
    REQUIRE(metal::mtl_format_to_iosurface(rgba32u, pf_uint, bpe));
    REQUIRE(pf_float == 'RGfA');
    REQUIRE(pf_uint == 'RGfA');
    REQUIRE(metal::mtl_format_to_storage_format(rgba32f, storage));
    REQUIRE(storage == static_cast<uint32_t>(texture_format::rgba32_float));
    REQUIRE(metal::mtl_format_to_storage_format(rgba32u, storage));
    REQUIRE(storage == static_cast<uint32_t>(texture_format::rgba32_uint));
    REQUIRE(metal::iosurface_format_accepts_mtl('RGfA', rgba32f));
    REQUIRE(metal::iosurface_format_accepts_mtl('RGfA', rgba32u));
    REQUIRE(metal::iosurface_format_requires_native_mtl('RGfA'));

    auto r32f = metal::nozzle_format_to_mtl(static_cast<uint32_t>(texture_format::r32_float));
    auto r32u = metal::nozzle_format_to_mtl(static_cast<uint32_t>(texture_format::r32_uint));
    REQUIRE(r32f != r32u);
    REQUIRE(metal::mtl_format_to_iosurface(r32f, pf_float, bpe));
    REQUIRE(metal::mtl_format_to_iosurface(r32u, pf_uint, bpe));
    REQUIRE(pf_float == 'L00f');
    REQUIRE(pf_uint == 'L00f');
    REQUIRE(metal::mtl_format_to_storage_format(r32f, storage));
    REQUIRE(storage == static_cast<uint32_t>(texture_format::r32_float));
    REQUIRE(metal::mtl_format_to_storage_format(r32u, storage));
    REQUIRE(storage == static_cast<uint32_t>(texture_format::r32_uint));
    REQUIRE(metal::iosurface_format_accepts_mtl('L00f', r32f));
    REQUIRE(metal::iosurface_format_accepts_mtl('L00f', r32u));
    REQUIRE(metal::iosurface_format_requires_native_mtl('L00f'));

    auto rgba16f = metal::nozzle_format_to_mtl(static_cast<uint32_t>(texture_format::rgba16_float));
    auto rgba16n = metal::nozzle_format_to_mtl(static_cast<uint32_t>(texture_format::rgba16_unorm));
    REQUIRE(rgba16f != rgba16n);
    REQUIRE(metal::mtl_format_to_iosurface(rgba16f, pf_float, bpe));
    REQUIRE(metal::mtl_format_to_iosurface(rgba16n, pf_uint, bpe));
    REQUIRE(pf_float == 'RGhA');
    REQUIRE(pf_uint == 'RGhA');
    REQUIRE(metal::mtl_format_to_storage_format(rgba16f, storage));
    REQUIRE(storage == static_cast<uint32_t>(texture_format::rgba16_float));
    REQUIRE(metal::mtl_format_to_storage_format(rgba16n, storage));
    REQUIRE(storage == static_cast<uint32_t>(texture_format::rgba16_unorm));
    REQUIRE(metal::iosurface_format_accepts_mtl('RGhA', rgba16f));
    REQUIRE(metal::iosurface_format_accepts_mtl('RGhA', rgba16n));
    REQUIRE(metal::iosurface_format_requires_native_mtl('RGhA'));
}

TEST_CASE("Metal IOSurface descriptor treats sRGB FourCC aliases as native-required", "[metal_format]") {
    auto rgba8 = metal::nozzle_format_to_mtl(static_cast<uint32_t>(texture_format::rgba8_unorm));
    auto rgba8_srgb = metal::nozzle_format_to_mtl(static_cast<uint32_t>(texture_format::rgba8_srgb));
    auto bgra8 = metal::nozzle_format_to_mtl(static_cast<uint32_t>(texture_format::bgra8_unorm));
    auto bgra8_srgb = metal::nozzle_format_to_mtl(static_cast<uint32_t>(texture_format::bgra8_srgb));
    REQUIRE(rgba8 != rgba8_srgb);
    REQUIRE(bgra8 != bgra8_srgb);
    REQUIRE(metal::iosurface_format_accepts_mtl('RGBA', rgba8));
    REQUIRE(metal::iosurface_format_accepts_mtl('RGBA', rgba8_srgb));
    REQUIRE(metal::iosurface_format_accepts_mtl('BGRA', bgra8));
    REQUIRE(metal::iosurface_format_accepts_mtl('BGRA', bgra8_srgb));
    REQUIRE(metal::iosurface_format_requires_native_mtl('RGBA'));
    REQUIRE(metal::iosurface_format_requires_native_mtl('BGRA'));
}

TEST_CASE("Metal IOSurface descriptor reports actual RGBA storage for RGB requests", "[metal_format]") {
    uint32_t storage = 0;

    auto rgb8 = metal::nozzle_format_to_mtl(static_cast<uint32_t>(texture_format::rgb8_unorm));
    REQUIRE(metal::mtl_format_to_storage_format(rgb8, storage));
    REQUIRE(storage == static_cast<uint32_t>(texture_format::rgba8_unorm));

    auto rgb16_unorm = metal::nozzle_format_to_mtl(static_cast<uint32_t>(texture_format::rgb16_unorm));
    REQUIRE(metal::mtl_format_to_storage_format(rgb16_unorm, storage));
    REQUIRE(storage == static_cast<uint32_t>(texture_format::rgba16_unorm));

    auto rgb16_float = metal::nozzle_format_to_mtl(static_cast<uint32_t>(texture_format::rgb16_float));
    REQUIRE(metal::mtl_format_to_storage_format(rgb16_float, storage));
    REQUIRE(storage == static_cast<uint32_t>(texture_format::rgba16_float));

    auto rgb32_float = metal::nozzle_format_to_mtl(static_cast<uint32_t>(texture_format::rgb32_float));
    REQUIRE(metal::mtl_format_to_storage_format(rgb32_float, storage));
    REQUIRE(storage == static_cast<uint32_t>(texture_format::rgba32_float));

    auto rgb32_uint = metal::nozzle_format_to_mtl(static_cast<uint32_t>(texture_format::rgb32_uint));
    REQUIRE(metal::mtl_format_to_storage_format(rgb32_uint, storage));
    REQUIRE(storage == static_cast<uint32_t>(texture_format::rgba32_uint));
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
        texture_format::rgba16_unorm,
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

#endif // NOZZLE_HAS_METAL
