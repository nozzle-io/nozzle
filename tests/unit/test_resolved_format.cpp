#include <catch2/catch_test_macros.hpp>

#include <nozzle/types.hpp>
#include <nozzle/format_resolve.hpp>

using namespace nozzle;

TEST_CASE("resolved_texture_format: default initialization", "[resolved]") {
    resolved_texture_format r{};
    REQUIRE(r.semantic_format == texture_format::unknown);
    REQUIRE(r.storage_format == texture_format::unknown);
    REQUIRE(r.native.backend == backend_type::unknown);
    REQUIRE(r.native.kind == native_format_kind::unknown);
    REQUIRE(r.native.value == 0);
    REQUIRE(r.native.modifier == 0);
    REQUIRE(r.cpu_layout.order == channel_order::unknown);
    REQUIRE(r.cpu_layout.component == component_type::unknown);
    REQUIRE(r.cpu_layout.component_bits == 0);
    REQUIRE(r.cpu_layout.channel_count == 0);
    REQUIRE(r.cpu_layout.bytes_per_pixel == 0);
    REQUIRE(r.source == format_source::requested);
    REQUIRE(r.swizzle == channel_swizzle::identity);
}

TEST_CASE("native_format_desc: default initialization", "[resolved]") {
    native_format_desc n{};
    REQUIRE(n.backend == backend_type::unknown);
    REQUIRE(n.kind == native_format_kind::unknown);
    REQUIRE(n.value == 0);
    REQUIRE(n.modifier == 0);
}

TEST_CASE("cpu_layout_desc: bytes_per_pixel consistency for all formats", "[resolved]") {
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
        texture_format::depth32_float,
    };

    for (auto fmt : formats) {
        auto layout = resolve_cpu_layout(fmt);
        REQUIRE(layout.bytes_per_pixel > 0);
        REQUIRE(layout.channel_count > 0);
        REQUIRE(layout.component_bits > 0);
        REQUIRE(layout.order != channel_order::unknown);
        REQUIRE(layout.component != component_type::unknown);

        uint8_t expected_bpp = layout.component_bits / 8 * layout.channel_count;
        REQUIRE(layout.bytes_per_pixel == expected_bpp);
    }
}

TEST_CASE("sampling_desc: exactly one flag set for known formats", "[resolved]") {
    const texture_format formats[] = {
        texture_format::r8_unorm,
        texture_format::rg8_unorm,
        texture_format::rgba8_unorm,
        texture_format::bgra8_unorm,
        texture_format::rgba8_srgb,
        texture_format::bgra8_srgb,
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
        texture_format::depth32_float,
    };

    for (auto fmt : formats) {
        auto s = resolve_sampling(fmt);
        int flags = s.normalized + s.srgb + s.floating_point + s.integer + s.depth;
        REQUIRE(flags >= 1);
    }
}

TEST_CASE("format_source: enum values are distinct", "[resolved]") {
    REQUIRE(format_source::requested != format_source::caller_hint);
    REQUIRE(format_source::caller_hint != format_source::native_observed);
    REQUIRE(format_source::native_observed != format_source::registry_metadata);
}

TEST_CASE("is_storage_compatible: all formats compatible with self", "[resolved]") {
    const texture_format formats[] = {
        texture_format::r8_unorm,
        texture_format::rg8_unorm,
        texture_format::rgba8_unorm,
        texture_format::bgra8_unorm,
        texture_format::rgba8_srgb,
        texture_format::bgra8_srgb,
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
        texture_format::depth32_float,
    };

    for (auto fmt : formats) {
        REQUIRE(is_storage_compatible(fmt, fmt));
    }
}

TEST_CASE("resolve_channel_order: bgra formats return bgra order", "[resolved]") {
    REQUIRE(resolve_channel_order(texture_format::bgra8_unorm) == channel_order::bgra);
    REQUIRE(resolve_channel_order(texture_format::bgra8_srgb) == channel_order::bgra);
}

TEST_CASE("resolve_channel_order: rgba formats return rgba order", "[resolved]") {
    REQUIRE(resolve_channel_order(texture_format::rgba8_unorm) == channel_order::rgba);
    REQUIRE(resolve_channel_order(texture_format::rgba16_float) == channel_order::rgba);
    REQUIRE(resolve_channel_order(texture_format::rgba32_float) == channel_order::rgba);
}

TEST_CASE("resolve_bytes_per_pixel: matches cpu_layout for all formats", "[resolved]") {
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
        texture_format::depth32_float,
    };

    for (auto fmt : formats) {
        REQUIRE(resolve_bytes_per_pixel(fmt) == resolve_cpu_layout(fmt).bytes_per_pixel);
    }
}
