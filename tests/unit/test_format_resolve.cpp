#include <catch2/catch_test_macros.hpp>

#include <nozzle/format_resolve.hpp>
#include <nozzle/types.hpp>

using namespace nozzle;

// ---------- resolve_cpu_layout ----------

TEST_CASE("resolve_cpu_layout: r8_unorm", "[format_resolve]") {
    auto l = resolve_cpu_layout(texture_format::r8_unorm);
    REQUIRE(l.order == channel_order::r);
    REQUIRE(l.component == component_type::unorm);
    REQUIRE(l.component_bits == 8);
    REQUIRE(l.channel_count == 1);
    REQUIRE(l.bytes_per_pixel == 1);
}

TEST_CASE("resolve_cpu_layout: rg8_unorm", "[format_resolve]") {
    auto l = resolve_cpu_layout(texture_format::rg8_unorm);
    REQUIRE(l.order == channel_order::rg);
    REQUIRE(l.component == component_type::unorm);
    REQUIRE(l.component_bits == 8);
    REQUIRE(l.channel_count == 2);
    REQUIRE(l.bytes_per_pixel == 2);
}

TEST_CASE("resolve_cpu_layout: rgba8_unorm", "[format_resolve]") {
    auto l = resolve_cpu_layout(texture_format::rgba8_unorm);
    REQUIRE(l.order == channel_order::rgba);
    REQUIRE(l.component == component_type::unorm);
    REQUIRE(l.component_bits == 8);
    REQUIRE(l.channel_count == 4);
    REQUIRE(l.bytes_per_pixel == 4);
}

TEST_CASE("resolve_cpu_layout: bgra8_unorm", "[format_resolve]") {
    auto l = resolve_cpu_layout(texture_format::bgra8_unorm);
    REQUIRE(l.order == channel_order::bgra);
    REQUIRE(l.component == component_type::unorm);
    REQUIRE(l.component_bits == 8);
    REQUIRE(l.channel_count == 4);
    REQUIRE(l.bytes_per_pixel == 4);
}

TEST_CASE("resolve_cpu_layout: srgb variants", "[format_resolve]") {
    auto l = resolve_cpu_layout(texture_format::rgba8_srgb);
    REQUIRE(l.order == channel_order::rgba);
    REQUIRE(l.component == component_type::unorm);
    REQUIRE(l.component_bits == 8);
    REQUIRE(l.channel_count == 4);
    REQUIRE(l.bytes_per_pixel == 4);

    l = resolve_cpu_layout(texture_format::bgra8_srgb);
    REQUIRE(l.order == channel_order::bgra);
    REQUIRE(l.component == component_type::unorm);
    REQUIRE(l.component_bits == 8);
    REQUIRE(l.channel_count == 4);
    REQUIRE(l.bytes_per_pixel == 4);
}

TEST_CASE("resolve_cpu_layout: 16-bit unorm", "[format_resolve]") {
    auto l = resolve_cpu_layout(texture_format::r16_unorm);
    REQUIRE(l.order == channel_order::r);
    REQUIRE(l.component == component_type::unorm);
    REQUIRE(l.component_bits == 16);
    REQUIRE(l.channel_count == 1);
    REQUIRE(l.bytes_per_pixel == 2);

    l = resolve_cpu_layout(texture_format::rg16_unorm);
    REQUIRE(l.channel_count == 2);
    REQUIRE(l.bytes_per_pixel == 4);

    l = resolve_cpu_layout(texture_format::rgba16_unorm);
    REQUIRE(l.channel_count == 4);
    REQUIRE(l.bytes_per_pixel == 8);
}

TEST_CASE("resolve_cpu_layout: 16-bit float", "[format_resolve]") {
    auto l = resolve_cpu_layout(texture_format::r16_float);
    REQUIRE(l.order == channel_order::r);
    REQUIRE(l.component == component_type::floating);
    REQUIRE(l.component_bits == 16);
    REQUIRE(l.channel_count == 1);
    REQUIRE(l.bytes_per_pixel == 2);

    l = resolve_cpu_layout(texture_format::rg16_float);
    REQUIRE(l.channel_count == 2);
    REQUIRE(l.bytes_per_pixel == 4);

    l = resolve_cpu_layout(texture_format::rgba16_float);
    REQUIRE(l.channel_count == 4);
    REQUIRE(l.bytes_per_pixel == 8);
}

TEST_CASE("resolve_cpu_layout: 32-bit float", "[format_resolve]") {
    auto l = resolve_cpu_layout(texture_format::r32_float);
    REQUIRE(l.order == channel_order::r);
    REQUIRE(l.component == component_type::floating);
    REQUIRE(l.component_bits == 32);
    REQUIRE(l.channel_count == 1);
    REQUIRE(l.bytes_per_pixel == 4);

    l = resolve_cpu_layout(texture_format::rg32_float);
    REQUIRE(l.channel_count == 2);
    REQUIRE(l.bytes_per_pixel == 8);

    l = resolve_cpu_layout(texture_format::rgba32_float);
    REQUIRE(l.channel_count == 4);
    REQUIRE(l.bytes_per_pixel == 16);
}

TEST_CASE("resolve_cpu_layout: uint", "[format_resolve]") {
    auto l = resolve_cpu_layout(texture_format::r32_uint);
    REQUIRE(l.order == channel_order::r);
    REQUIRE(l.component == component_type::uint);
    REQUIRE(l.component_bits == 32);
    REQUIRE(l.channel_count == 1);
    REQUIRE(l.bytes_per_pixel == 4);

    l = resolve_cpu_layout(texture_format::rgba32_uint);
    REQUIRE(l.channel_count == 4);
    REQUIRE(l.bytes_per_pixel == 16);
}

TEST_CASE("resolve_cpu_layout: depth32_float", "[format_resolve]") {
    auto l = resolve_cpu_layout(texture_format::depth32_float);
    REQUIRE(l.order == channel_order::r);
    REQUIRE(l.component == component_type::depth);
    REQUIRE(l.component_bits == 32);
    REQUIRE(l.channel_count == 1);
    REQUIRE(l.bytes_per_pixel == 4);
}

TEST_CASE("resolve_cpu_layout: unknown returns zeroed", "[format_resolve]") {
    auto l = resolve_cpu_layout(texture_format::unknown);
    REQUIRE(l.order == channel_order::unknown);
    REQUIRE(l.component == component_type::unknown);
    REQUIRE(l.component_bits == 0);
    REQUIRE(l.channel_count == 0);
    REQUIRE(l.bytes_per_pixel == 0);
}

// ---------- resolve_sampling ----------

TEST_CASE("resolve_sampling: unorm variants are normalized", "[format_resolve]") {
    REQUIRE(resolve_sampling(texture_format::r8_unorm).normalized == true);
    REQUIRE(resolve_sampling(texture_format::rg8_unorm).normalized == true);
    REQUIRE(resolve_sampling(texture_format::rgba8_unorm).normalized == true);
    REQUIRE(resolve_sampling(texture_format::bgra8_unorm).normalized == true);
    REQUIRE(resolve_sampling(texture_format::r16_unorm).normalized == true);
    REQUIRE(resolve_sampling(texture_format::rg16_unorm).normalized == true);
    REQUIRE(resolve_sampling(texture_format::rgba16_unorm).normalized == true);
}

TEST_CASE("resolve_sampling: srgb variants are srgb+normalized", "[format_resolve]") {
    auto s = resolve_sampling(texture_format::rgba8_srgb);
    REQUIRE(s.srgb == true);
    REQUIRE(s.normalized == true);

    s = resolve_sampling(texture_format::bgra8_srgb);
    REQUIRE(s.srgb == true);
    REQUIRE(s.normalized == true);
}

TEST_CASE("resolve_sampling: float variants are floating_point", "[format_resolve]") {
    REQUIRE(resolve_sampling(texture_format::r16_float).floating_point == true);
    REQUIRE(resolve_sampling(texture_format::rg16_float).floating_point == true);
    REQUIRE(resolve_sampling(texture_format::rgba16_float).floating_point == true);
    REQUIRE(resolve_sampling(texture_format::r32_float).floating_point == true);
    REQUIRE(resolve_sampling(texture_format::rg32_float).floating_point == true);
    REQUIRE(resolve_sampling(texture_format::rgba32_float).floating_point == true);
}

TEST_CASE("resolve_sampling: uint variants are integer", "[format_resolve]") {
    REQUIRE(resolve_sampling(texture_format::r32_uint).integer == true);
    REQUIRE(resolve_sampling(texture_format::rgba32_uint).integer == true);
}

TEST_CASE("resolve_sampling: depth32_float is depth", "[format_resolve]") {
    auto s = resolve_sampling(texture_format::depth32_float);
    REQUIRE(s.depth == true);
    REQUIRE(s.floating_point == false);
    REQUIRE(s.normalized == false);
}

TEST_CASE("resolve_sampling: unknown returns all false", "[format_resolve]") {
    auto s = resolve_sampling(texture_format::unknown);
    REQUIRE(s.srgb == false);
    REQUIRE(s.normalized == false);
    REQUIRE(s.integer == false);
    REQUIRE(s.floating_point == false);
    REQUIRE(s.depth == false);
}

// ---------- convenience accessors ----------

TEST_CASE("resolve_channel_order delegates to resolve_cpu_layout", "[format_resolve]") {
    REQUIRE(resolve_channel_order(texture_format::r8_unorm) == channel_order::r);
    REQUIRE(resolve_channel_order(texture_format::bgra8_unorm) == channel_order::bgra);
    REQUIRE(resolve_channel_order(texture_format::unknown) == channel_order::unknown);
}

TEST_CASE("resolve_component_type delegates to resolve_cpu_layout", "[format_resolve]") {
    REQUIRE(resolve_component_type(texture_format::r8_unorm) == component_type::unorm);
    REQUIRE(resolve_component_type(texture_format::r32_float) == component_type::floating);
    REQUIRE(resolve_component_type(texture_format::r32_uint) == component_type::uint);
    REQUIRE(resolve_component_type(texture_format::unknown) == component_type::unknown);
}

TEST_CASE("resolve_bytes_per_pixel delegates to resolve_cpu_layout", "[format_resolve]") {
    REQUIRE(resolve_bytes_per_pixel(texture_format::r8_unorm) == 1);
    REQUIRE(resolve_bytes_per_pixel(texture_format::rgba8_unorm) == 4);
    REQUIRE(resolve_bytes_per_pixel(texture_format::rgba32_float) == 16);
    REQUIRE(resolve_bytes_per_pixel(texture_format::unknown) == 0);
}

TEST_CASE("resolve_channel_count delegates to resolve_cpu_layout", "[format_resolve]") {
    REQUIRE(resolve_channel_count(texture_format::r8_unorm) == 1);
    REQUIRE(resolve_channel_count(texture_format::rg8_unorm) == 2);
    REQUIRE(resolve_channel_count(texture_format::rgba8_unorm) == 4);
    REQUIRE(resolve_channel_count(texture_format::unknown) == 0);
}

TEST_CASE("resolve_component_bits delegates to resolve_cpu_layout", "[format_resolve]") {
    REQUIRE(resolve_component_bits(texture_format::r8_unorm) == 8);
    REQUIRE(resolve_component_bits(texture_format::r16_float) == 16);
    REQUIRE(resolve_component_bits(texture_format::r32_float) == 32);
    REQUIRE(resolve_component_bits(texture_format::unknown) == 0);
}

// ---------- is_storage_compatible ----------

TEST_CASE("is_storage_compatible: identical formats", "[format_resolve]") {
    REQUIRE(is_storage_compatible(texture_format::rgba8_unorm, texture_format::rgba8_unorm) == true);
    REQUIRE(is_storage_compatible(texture_format::r32_float, texture_format::r32_float) == true);
}

TEST_CASE("is_storage_compatible: rgba8_unorm <-> bgra8_unorm", "[format_resolve]") {
    REQUIRE(is_storage_compatible(texture_format::rgba8_unorm, texture_format::bgra8_unorm) == true);
    REQUIRE(is_storage_compatible(texture_format::bgra8_unorm, texture_format::rgba8_unorm) == true);
}

TEST_CASE("is_storage_compatible: srgb <-> linear same channel layout", "[format_resolve]") {
    REQUIRE(is_storage_compatible(texture_format::rgba8_unorm, texture_format::rgba8_srgb) == true);
    REQUIRE(is_storage_compatible(texture_format::bgra8_unorm, texture_format::bgra8_srgb) == true);
    REQUIRE(is_storage_compatible(texture_format::rgba8_srgb, texture_format::rgba8_unorm) == true);
}

TEST_CASE("is_storage_compatible: different bit depth is NOT compatible", "[format_resolve]") {
    REQUIRE(is_storage_compatible(texture_format::rgba8_unorm, texture_format::rgba16_unorm) == false);
    REQUIRE(is_storage_compatible(texture_format::r32_float, texture_format::r16_float) == false);
}

TEST_CASE("is_storage_compatible: different channel count is NOT compatible", "[format_resolve]") {
    REQUIRE(is_storage_compatible(texture_format::r8_unorm, texture_format::rg8_unorm) == false);
    REQUIRE(is_storage_compatible(texture_format::r8_unorm, texture_format::rgba8_unorm) == false);
    REQUIRE(is_storage_compatible(texture_format::rg8_unorm, texture_format::rgba8_unorm) == false);
}

TEST_CASE("is_storage_compatible: different component type is NOT compatible", "[format_resolve]") {
    REQUIRE(is_storage_compatible(texture_format::r8_unorm, texture_format::r32_uint) == false);
    REQUIRE(is_storage_compatible(texture_format::r32_float, texture_format::r32_uint) == false);
}

TEST_CASE("is_storage_compatible: depth is NOT compatible with float", "[format_resolve]") {
    REQUIRE(is_storage_compatible(texture_format::depth32_float, texture_format::r32_float) == false);
}

TEST_CASE("is_storage_compatible: unknown with anything is NOT compatible", "[format_resolve]") {
    REQUIRE(is_storage_compatible(texture_format::unknown, texture_format::rgba8_unorm) == false);
    REQUIRE(is_storage_compatible(texture_format::rgba8_unorm, texture_format::unknown) == false);
    REQUIRE(is_storage_compatible(texture_format::unknown, texture_format::unknown) == false);
}

// ---------- get_storage_compatible_fallback ----------

TEST_CASE("get_storage_compatible_fallback: rgba8_unorm -> bgra8_unorm", "[format_resolve]") {
    REQUIRE(get_storage_compatible_fallback(texture_format::rgba8_unorm) == texture_format::bgra8_unorm);
}

TEST_CASE("get_storage_compatible_fallback: bgra8_unorm -> rgba8_unorm", "[format_resolve]") {
    REQUIRE(get_storage_compatible_fallback(texture_format::bgra8_unorm) == texture_format::rgba8_unorm);
}

TEST_CASE("get_storage_compatible_fallback: no fallback for other formats", "[format_resolve]") {
    REQUIRE(get_storage_compatible_fallback(texture_format::r8_unorm) == texture_format::unknown);
    REQUIRE(get_storage_compatible_fallback(texture_format::r32_float) == texture_format::unknown);
    REQUIRE(get_storage_compatible_fallback(texture_format::rgba16_float) == texture_format::unknown);
    REQUIRE(get_storage_compatible_fallback(texture_format::unknown) == texture_format::unknown);
}
