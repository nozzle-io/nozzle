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

TEST_CASE("resolve_cpu_layout: rgb8_unorm", "[format_resolve]") {
    auto l = resolve_cpu_layout(texture_format::rgb8_unorm);
    REQUIRE(l.order == channel_order::rgb);
    REQUIRE(l.component == component_type::unorm);
    REQUIRE(l.component_bits == 8);
    REQUIRE(l.channel_count == 3);
    REQUIRE(l.bytes_per_pixel == 3);
}

TEST_CASE("resolve_cpu_layout: rgb16_unorm", "[format_resolve]") {
    auto l = resolve_cpu_layout(texture_format::rgb16_unorm);
    REQUIRE(l.order == channel_order::rgb);
    REQUIRE(l.component == component_type::unorm);
    REQUIRE(l.component_bits == 16);
    REQUIRE(l.channel_count == 3);
    REQUIRE(l.bytes_per_pixel == 6);
}

TEST_CASE("resolve_cpu_layout: rgb16_float", "[format_resolve]") {
    auto l = resolve_cpu_layout(texture_format::rgb16_float);
    REQUIRE(l.order == channel_order::rgb);
    REQUIRE(l.component == component_type::floating);
    REQUIRE(l.component_bits == 16);
    REQUIRE(l.channel_count == 3);
    REQUIRE(l.bytes_per_pixel == 6);
}

TEST_CASE("resolve_cpu_layout: rgb32_float", "[format_resolve]") {
    auto l = resolve_cpu_layout(texture_format::rgb32_float);
    REQUIRE(l.order == channel_order::rgb);
    REQUIRE(l.component == component_type::floating);
    REQUIRE(l.component_bits == 32);
    REQUIRE(l.channel_count == 3);
    REQUIRE(l.bytes_per_pixel == 12);
}

TEST_CASE("resolve_cpu_layout: rgb32_uint", "[format_resolve]") {
    auto l = resolve_cpu_layout(texture_format::rgb32_uint);
    REQUIRE(l.order == channel_order::rgb);
    REQUIRE(l.component == component_type::uint);
    REQUIRE(l.component_bits == 32);
    REQUIRE(l.channel_count == 3);
    REQUIRE(l.bytes_per_pixel == 12);
}

// ---------- resolve_sampling ----------

TEST_CASE("resolve_sampling: unorm variants are normalized", "[format_resolve]") {
    REQUIRE(resolve_sampling(texture_format::r8_unorm).normalized == true);
    REQUIRE(resolve_sampling(texture_format::rg8_unorm).normalized == true);
    REQUIRE(resolve_sampling(texture_format::rgb8_unorm).normalized == true);
    REQUIRE(resolve_sampling(texture_format::rgba8_unorm).normalized == true);
    REQUIRE(resolve_sampling(texture_format::bgra8_unorm).normalized == true);
    REQUIRE(resolve_sampling(texture_format::r16_unorm).normalized == true);
    REQUIRE(resolve_sampling(texture_format::rg16_unorm).normalized == true);
    REQUIRE(resolve_sampling(texture_format::rgb16_unorm).normalized == true);
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
    REQUIRE(resolve_sampling(texture_format::rgb16_float).floating_point == true);
    REQUIRE(resolve_sampling(texture_format::rgba16_float).floating_point == true);
    REQUIRE(resolve_sampling(texture_format::r32_float).floating_point == true);
    REQUIRE(resolve_sampling(texture_format::rg32_float).floating_point == true);
    REQUIRE(resolve_sampling(texture_format::rgb32_float).floating_point == true);
    REQUIRE(resolve_sampling(texture_format::rgba32_float).floating_point == true);
}

TEST_CASE("resolve_sampling: uint variants are integer", "[format_resolve]") {
    REQUIRE(resolve_sampling(texture_format::r32_uint).integer == true);
    REQUIRE(resolve_sampling(texture_format::rgba32_uint).integer == true);
    REQUIRE(resolve_sampling(texture_format::rgb32_uint).integer == true);
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
    REQUIRE(get_storage_compatible_fallback(texture_format::rgb8_unorm) == texture_format::unknown);
    REQUIRE(get_storage_compatible_fallback(texture_format::rgb16_unorm) == texture_format::unknown);
    REQUIRE(get_storage_compatible_fallback(texture_format::rgb16_float) == texture_format::unknown);
    REQUIRE(get_storage_compatible_fallback(texture_format::rgb32_float) == texture_format::unknown);
    REQUIRE(get_storage_compatible_fallback(texture_format::rgb32_uint) == texture_format::unknown);
}

// ---------- resolve_cpu_layout: all formats have valid bytes_per_pixel (P2-T8) ----------

TEST_CASE("resolve_cpu_layout: every format returns positive bytes_per_pixel", "[format_resolve]") {
    const texture_format all[] = {
        texture_format::r8_unorm, texture_format::rg8_unorm,
        texture_format::rgb8_unorm,
        texture_format::rgba8_unorm, texture_format::bgra8_unorm,
        texture_format::rgba8_srgb, texture_format::bgra8_srgb,
        texture_format::r16_unorm, texture_format::rg16_unorm,
        texture_format::rgb16_unorm,
        texture_format::rgba16_unorm,
        texture_format::r16_float, texture_format::rg16_float,
        texture_format::rgb16_float,
        texture_format::rgba16_float,
        texture_format::r32_float, texture_format::rg32_float,
        texture_format::rgb32_float,
        texture_format::rgba32_float,
        texture_format::r32_uint, texture_format::rgba32_uint,
        texture_format::rgb32_uint,
        texture_format::depth32_float,
    };
    for (auto fmt : all) {
        auto l = resolve_cpu_layout(fmt);
        REQUIRE(l.bytes_per_pixel > 0);
        REQUIRE(l.channel_count > 0);
    }
}

TEST_CASE("resolve_cpu_layout: 8-bit formats have 1 byte per component", "[format_resolve]") {
    REQUIRE(resolve_cpu_layout(texture_format::r8_unorm).bytes_per_pixel == 1);
    REQUIRE(resolve_cpu_layout(texture_format::rg8_unorm).bytes_per_pixel == 2);
    REQUIRE(resolve_cpu_layout(texture_format::rgba8_unorm).bytes_per_pixel == 4);
    REQUIRE(resolve_cpu_layout(texture_format::bgra8_unorm).bytes_per_pixel == 4);
}

TEST_CASE("resolve_cpu_layout: 16-bit formats have 2 bytes per component", "[format_resolve]") {
    REQUIRE(resolve_cpu_layout(texture_format::r16_unorm).bytes_per_pixel == 2);
    REQUIRE(resolve_cpu_layout(texture_format::rg16_unorm).bytes_per_pixel == 4);
    REQUIRE(resolve_cpu_layout(texture_format::rgba16_unorm).bytes_per_pixel == 8);
    REQUIRE(resolve_cpu_layout(texture_format::r16_float).bytes_per_pixel == 2);
    REQUIRE(resolve_cpu_layout(texture_format::rg16_float).bytes_per_pixel == 4);
    REQUIRE(resolve_cpu_layout(texture_format::rgba16_float).bytes_per_pixel == 8);
}

TEST_CASE("resolve_cpu_layout: 32-bit formats have 4 bytes per component", "[format_resolve]") {
    REQUIRE(resolve_cpu_layout(texture_format::r32_float).bytes_per_pixel == 4);
    REQUIRE(resolve_cpu_layout(texture_format::rg32_float).bytes_per_pixel == 8);
    REQUIRE(resolve_cpu_layout(texture_format::rgba32_float).bytes_per_pixel == 16);
    REQUIRE(resolve_cpu_layout(texture_format::r32_uint).bytes_per_pixel == 4);
    REQUIRE(resolve_cpu_layout(texture_format::rgba32_uint).bytes_per_pixel == 16);
    REQUIRE(resolve_cpu_layout(texture_format::depth32_float).bytes_per_pixel == 4);
}

TEST_CASE("get_channel_expansion_fallback: 3ch formats map to 4ch", "[format_resolve]") {
    REQUIRE(get_channel_expansion_fallback(texture_format::rgb8_unorm) == texture_format::rgba8_unorm);
    REQUIRE(get_channel_expansion_fallback(texture_format::rgb16_unorm) == texture_format::rgba16_unorm);
    REQUIRE(get_channel_expansion_fallback(texture_format::rgb16_float) == texture_format::rgba16_float);
    REQUIRE(get_channel_expansion_fallback(texture_format::rgb32_float) == texture_format::rgba32_float);
    REQUIRE(get_channel_expansion_fallback(texture_format::rgb32_uint) == texture_format::rgba32_uint);
}

TEST_CASE("get_channel_expansion_fallback: no fallback for non-rgb formats", "[format_resolve]") {
    REQUIRE(get_channel_expansion_fallback(texture_format::rgba8_unorm) == texture_format::unknown);
    REQUIRE(get_channel_expansion_fallback(texture_format::r8_unorm) == texture_format::unknown);
    REQUIRE(get_channel_expansion_fallback(texture_format::r32_float) == texture_format::unknown);
    REQUIRE(get_channel_expansion_fallback(texture_format::unknown) == texture_format::unknown);
}

// ---------- resolve_fallback (#31) ----------

TEST_CASE("resolve_fallback: storage compatible rgba8->bgra8", "[format_resolve]") {
    auto fb = resolve_fallback(texture_format::rgba8_unorm, fallback_allow_storage_compatible);
    REQUIRE(fb.valid);
    REQUIRE(fb.target == texture_format::bgra8_unorm);
    REQUIRE(fb.category == fallback_category::storage_compatible);
}

TEST_CASE("resolve_fallback: storage compatible bgra8->rgba8", "[format_resolve]") {
    auto fb = resolve_fallback(texture_format::bgra8_unorm, fallback_allow_storage_compatible);
    REQUIRE(fb.valid);
    REQUIRE(fb.target == texture_format::rgba8_unorm);
    REQUIRE(fb.category == fallback_category::storage_compatible);
}

TEST_CASE("resolve_fallback: channel expansion rgb8->rgba8", "[format_resolve]") {
    auto fb = resolve_fallback(texture_format::rgb8_unorm, fallback_allow_channel_expansion);
    REQUIRE(fb.valid);
    REQUIRE(fb.target == texture_format::rgba8_unorm);
    REQUIRE(fb.category == fallback_category::channel_expansion);
}

TEST_CASE("resolve_fallback: channel expansion rgb32_float->rgba32_float", "[format_resolve]") {
    auto fb = resolve_fallback(texture_format::rgb32_float, fallback_allow_channel_expansion);
    REQUIRE(fb.valid);
    REQUIRE(fb.target == texture_format::rgba32_float);
    REQUIRE(fb.category == fallback_category::channel_expansion);
}

TEST_CASE("resolve_fallback: quality loss r32_float->r16_float with flag", "[format_resolve]") {
    auto fb = resolve_fallback(texture_format::r32_float, fallback_allow_quality_loss);
    REQUIRE(fb.valid);
    REQUIRE(fb.target == texture_format::r16_float);
    REQUIRE(fb.category == fallback_category::quality_loss);
}

TEST_CASE("resolve_fallback: quality loss rejected without flag", "[format_resolve]") {
    auto fb = resolve_fallback(texture_format::r32_float, fallback_none);
    REQUIRE_FALSE(fb.valid);

    fb = resolve_fallback(texture_format::r32_float, fallback_allow_storage_compatible);
    REQUIRE_FALSE(fb.valid);

    fb = resolve_fallback(texture_format::r32_float, fallback_allow_channel_expansion);
    REQUIRE_FALSE(fb.valid);
}

TEST_CASE("resolve_fallback: no chain — rgb32_float with expansion+quality_loss stays at rgba32_float", "[format_resolve]") {
    auto fb = resolve_fallback(texture_format::rgb32_float,
        fallback_allow_channel_expansion | fallback_allow_quality_loss);
    REQUIRE(fb.valid);
    REQUIRE(fb.target == texture_format::rgba32_float);
    REQUIRE(fb.category == fallback_category::channel_expansion);
}

TEST_CASE("resolve_fallback: r8_unorm has no storage or expansion fallback", "[format_resolve]") {
    auto fb = resolve_fallback(texture_format::r8_unorm, fallback_safe_defaults);
    REQUIRE_FALSE(fb.valid);
}

TEST_CASE("resolve_fallback: no fallback for unknown format", "[format_resolve]") {
    auto fb = resolve_fallback(texture_format::unknown, fallback_safe_defaults);
    REQUIRE_FALSE(fb.valid);
}

// ---------- is_allowed_storage_fallback ----------

TEST_CASE("is_allowed_storage_fallback: rgba8<->bgra8", "[format_resolve]") {
    REQUIRE(is_allowed_storage_fallback(texture_format::rgba8_unorm, texture_format::bgra8_unorm));
    REQUIRE(is_allowed_storage_fallback(texture_format::bgra8_unorm, texture_format::rgba8_unorm));
}

TEST_CASE("is_allowed_storage_fallback: rejects srgb pair", "[format_resolve]") {
    REQUIRE_FALSE(is_allowed_storage_fallback(texture_format::rgba8_unorm, texture_format::rgba8_srgb));
    REQUIRE_FALSE(is_allowed_storage_fallback(texture_format::rgba8_unorm, texture_format::bgra8_srgb));
}

TEST_CASE("is_allowed_storage_fallback: rejects unknown", "[format_resolve]") {
    REQUIRE_FALSE(is_allowed_storage_fallback(texture_format::unknown, texture_format::rgba8_unorm));
}

// ---------- classify_reuse ----------

TEST_CASE("classify_reuse: exact match", "[format_resolve]") {
    auto m = classify_reuse(texture_format::rgba8_unorm, texture_format::rgba8_unorm, fallback_safe_defaults);
    REQUIRE(m == reuse_match::exact);
}

TEST_CASE("classify_reuse: storage compatible with flag", "[format_resolve]") {
    auto m = classify_reuse(texture_format::bgra8_unorm, texture_format::rgba8_unorm, fallback_allow_storage_compatible);
    REQUIRE(m == reuse_match::storage_compatible);
}

TEST_CASE("classify_reuse: storage compatible rejected without flag", "[format_resolve]") {
    auto m = classify_reuse(texture_format::bgra8_unorm, texture_format::rgba8_unorm, fallback_none);
    REQUIRE(m == reuse_match::disallowed);

    m = classify_reuse(texture_format::bgra8_unorm, texture_format::rgba8_unorm, fallback_allow_channel_expansion);
    REQUIRE(m == reuse_match::disallowed);
}

TEST_CASE("classify_reuse: different bit depth disallowed", "[format_resolve]") {
    auto m = classify_reuse(texture_format::rgba8_unorm, texture_format::rgba16_float, fallback_safe_defaults);
    REQUIRE(m == reuse_match::disallowed);
}

// ---------- derive_swizzle ----------

TEST_CASE("derive_swizzle: identity for same format", "[format_resolve]") {
    auto r = derive_swizzle(texture_format::rgba8_unorm, texture_format::rgba8_unorm);
    REQUIRE(r.ok());
    REQUIRE(r.value() == channel_swizzle::identity);
}

TEST_CASE("derive_swizzle: swap_rb for rgba8<->bgra8", "[format_resolve]") {
    auto r = derive_swizzle(texture_format::rgba8_unorm, texture_format::bgra8_unorm);
    REQUIRE(r.ok());
    REQUIRE(r.value() == channel_swizzle::swap_rb);

    r = derive_swizzle(texture_format::bgra8_unorm, texture_format::rgba8_unorm);
    REQUIRE(r.ok());
    REQUIRE(r.value() == channel_swizzle::swap_rb);
}

TEST_CASE("derive_swizzle: error for unhandled pair", "[format_resolve]") {
    auto r = derive_swizzle(texture_format::rgba8_unorm, texture_format::rgba16_float);
    REQUIRE_FALSE(r.ok());
    REQUIRE(r.error().code == ErrorCode::UnsupportedFormat);
}

// ---------- fallback_category_name ----------

TEST_CASE("fallback_category_name: all categories", "[format_resolve]") {
    REQUIRE(std::string(fallback_category_name(fallback_category::none)) == "none");
    REQUIRE(std::string(fallback_category_name(fallback_category::storage_compatible)) == "storage-compatible");
    REQUIRE(std::string(fallback_category_name(fallback_category::channel_expansion)) == "channel-expansion");
    REQUIRE(std::string(fallback_category_name(fallback_category::quality_loss)) == "quality-loss");
}

// ---------- classify_observed_format ----------

TEST_CASE("classify_observed_format: exact match -> none", "[format_resolve]") {
    auto r = classify_observed_format(
        texture_format::rgba8_unorm, texture_format::rgba8_unorm,
        fallback_category::none, texture_format::unknown,
        fallback_safe_defaults);
    REQUIRE(r.ok());
    REQUIRE(r.value() == fallback_category::none);
}

TEST_CASE("classify_observed_format: fallback target matches observed", "[format_resolve]") {
    auto r = classify_observed_format(
        texture_format::rgba8_unorm, texture_format::bgra8_unorm,
        fallback_category::storage_compatible, texture_format::bgra8_unorm,
        fallback_safe_defaults);
    REQUIRE(r.ok());
    REQUIRE(r.value() == fallback_category::storage_compatible);
}

TEST_CASE("classify_observed_format: backend returns allowed storage without explicit fallback", "[format_resolve]") {
    auto r = classify_observed_format(
        texture_format::rgba8_unorm, texture_format::bgra8_unorm,
        fallback_category::none, texture_format::unknown,
        fallback_safe_defaults);
    REQUIRE(r.ok());
    REQUIRE(r.value() == fallback_category::storage_compatible);
}

TEST_CASE("classify_observed_format: backend returns unexpected format -> error", "[format_resolve]") {
    auto r = classify_observed_format(
        texture_format::rgba8_unorm, texture_format::rgba16_float,
        fallback_category::none, texture_format::unknown,
        fallback_safe_defaults);
    REQUIRE_FALSE(r.ok());
}

TEST_CASE("classify_observed_format: fallback target mismatch -> error", "[format_resolve]") {
    auto r = classify_observed_format(
        texture_format::rgba8_unorm, texture_format::rgba16_float,
        fallback_category::storage_compatible, texture_format::bgra8_unorm,
        fallback_safe_defaults);
    REQUIRE_FALSE(r.ok());
}

TEST_CASE("classify_observed_format: storage not allowed when flag absent -> error", "[format_resolve]") {
    auto r = classify_observed_format(
        texture_format::rgba8_unorm, texture_format::bgra8_unorm,
        fallback_category::none, texture_format::unknown,
        fallback_allow_channel_expansion);
    REQUIRE_FALSE(r.ok());
}

TEST_CASE("classify_observed_format: channel expansion with storage-compatible observed (rgb8->rgba8->bgra8)", "[format_resolve]") {
    auto r = classify_observed_format(
        texture_format::rgb8_unorm, texture_format::bgra8_unorm,
        fallback_category::channel_expansion, texture_format::rgba8_unorm,
        fallback_safe_defaults);
    REQUIRE(r.ok());
    REQUIRE(r.value() == fallback_category::channel_expansion);
}

TEST_CASE("classify_observed_format: channel expansion with storage-incompatible observed -> error", "[format_resolve]") {
    auto r = classify_observed_format(
        texture_format::rgb8_unorm, texture_format::rgba16_float,
        fallback_category::channel_expansion, texture_format::rgba8_unorm,
        fallback_safe_defaults);
    REQUIRE_FALSE(r.ok());
}

TEST_CASE("classify_observed_format: channel expansion with storage-compatible observed but storage flag absent -> error", "[format_resolve]") {
    auto r = classify_observed_format(
        texture_format::rgb8_unorm, texture_format::bgra8_unorm,
        fallback_category::channel_expansion, texture_format::rgba8_unorm,
        fallback_allow_channel_expansion);
    REQUIRE_FALSE(r.ok());
}

// ---------- resolve_fallback_metadata ----------

TEST_CASE("resolve_fallback_metadata: exact match -> identity swizzle", "[format_resolve]") {
    auto r = resolve_fallback_metadata(
        texture_format::rgba8_unorm, texture_format::rgba8_unorm,
        fallback_category::none, texture_format::unknown);
    REQUIRE(r.ok());
    REQUIRE(r.value().semantic_format == texture_format::rgba8_unorm);
    REQUIRE(r.value().storage_format == texture_format::rgba8_unorm);
    REQUIRE(r.value().swizzle == channel_swizzle::identity);
}

TEST_CASE("resolve_fallback_metadata: storage-compatible -> swap_rb", "[format_resolve]") {
    auto r = resolve_fallback_metadata(
        texture_format::rgba8_unorm, texture_format::bgra8_unorm,
        fallback_category::storage_compatible, texture_format::unknown);
    REQUIRE(r.ok());
    REQUIRE(r.value().semantic_format == texture_format::rgba8_unorm);
    REQUIRE(r.value().storage_format == texture_format::bgra8_unorm);
    REQUIRE(r.value().swizzle == channel_swizzle::swap_rb);
}

TEST_CASE("resolve_fallback_metadata: channel expansion exact target -> identity", "[format_resolve]") {
    auto r = resolve_fallback_metadata(
        texture_format::rgb8_unorm, texture_format::rgba8_unorm,
        fallback_category::channel_expansion, texture_format::rgba8_unorm);
    REQUIRE(r.ok());
    REQUIRE(r.value().semantic_format == texture_format::rgb8_unorm);
    REQUIRE(r.value().storage_format == texture_format::rgba8_unorm);
    REQUIRE(r.value().swizzle == channel_swizzle::identity);
}

TEST_CASE("resolve_fallback_metadata: channel expansion + storage observed -> swap_rb", "[format_resolve]") {
    auto r = resolve_fallback_metadata(
        texture_format::rgb8_unorm, texture_format::bgra8_unorm,
        fallback_category::channel_expansion, texture_format::rgba8_unorm);
    REQUIRE(r.ok());
    REQUIRE(r.value().semantic_format == texture_format::rgb8_unorm);
    REQUIRE(r.value().storage_format == texture_format::bgra8_unorm);
    REQUIRE(r.value().swizzle == channel_swizzle::swap_rb);
}

TEST_CASE("resolve_fallback_metadata: none with mismatch -> error", "[format_resolve]") {
    auto r = resolve_fallback_metadata(
        texture_format::rgba8_unorm, texture_format::bgra8_unorm,
        fallback_category::none, texture_format::unknown);
    REQUIRE_FALSE(r.ok());
}

TEST_CASE("resolve_fallback_metadata: quality loss with storage observed -> swap_rb", "[format_resolve]") {
    auto r = resolve_fallback_metadata(
        texture_format::rgba16_float, texture_format::bgra8_unorm,
        fallback_category::quality_loss, texture_format::rgba8_unorm);
    REQUIRE(r.ok());
    REQUIRE(r.value().semantic_format == texture_format::rgba16_float);
    REQUIRE(r.value().storage_format == texture_format::bgra8_unorm);
    REQUIRE(r.value().swizzle == channel_swizzle::swap_rb);
}

// ---------- validate_fallback_flags ----------

TEST_CASE("validate_fallback_flags: safe defaults -> ok", "[format_resolve]") {
    auto r = validate_fallback_flags(fallback_safe_defaults);
    REQUIRE(r.ok());
}

TEST_CASE("validate_fallback_flags: none -> ok", "[format_resolve]") {
    auto r = validate_fallback_flags(fallback_none);
    REQUIRE(r.ok());
}

TEST_CASE("validate_fallback_flags: unknown bits -> error", "[format_resolve]") {
    auto r = validate_fallback_flags(0x80000000u | fallback_allow_storage_compatible);
    REQUIRE_FALSE(r.ok());
    REQUIRE(r.error().code == ErrorCode::InvalidArgument);
}

TEST_CASE("validate_fallback_flags: only unknown bits -> error", "[format_resolve]") {
    auto r = validate_fallback_flags(0xFF000000u);
    REQUIRE_FALSE(r.ok());
    REQUIRE(r.error().code == ErrorCode::InvalidArgument);
}

// ---------- #35: single-step fallback contract ----------

TEST_CASE("#35: quality-loss opt-in — rgba32_float -> rgba16_float only with flag", "[format_resolve]") {
    auto fb = resolve_fallback(texture_format::rgba32_float, fallback_allow_quality_loss);
    REQUIRE(fb.valid);
    REQUIRE(fb.target == texture_format::rgba16_float);
    REQUIRE(fb.category == fallback_category::quality_loss);

    fb = resolve_fallback(texture_format::rgba32_float, fallback_safe_defaults);
    REQUIRE_FALSE(fb.valid);

    fb = resolve_fallback(texture_format::rgba32_float, fallback_none);
    REQUIRE_FALSE(fb.valid);
}

TEST_CASE("#35: quality-loss opt-in — rgba16_float -> rgba8_unorm only with flag", "[format_resolve]") {
    auto fb = resolve_fallback(texture_format::rgba16_float, fallback_allow_quality_loss);
    REQUIRE(fb.valid);
    REQUIRE(fb.target == texture_format::rgba8_unorm);
    REQUIRE(fb.category == fallback_category::quality_loss);

    fb = resolve_fallback(texture_format::rgba16_float, fallback_safe_defaults);
    REQUIRE_FALSE(fb.valid);
}

TEST_CASE("#35: no chain — resolve_fallback of quality-loss target returns lower quality, but sender must not apply it", "[format_resolve]") {
    auto fb = resolve_fallback(texture_format::r16_float, fallback_allow_quality_loss);
    REQUIRE(fb.valid);
    REQUIRE(fb.target == texture_format::r8_unorm);

    auto fb2 = resolve_fallback(texture_format::rgba16_float, fallback_allow_quality_loss);
    REQUIRE(fb2.valid);
    REQUIRE(fb2.target == texture_format::rgba8_unorm);
}

TEST_CASE("#35: quality-loss maps all supported formats", "[format_resolve]") {
    auto fb = resolve_fallback(texture_format::rg32_float, fallback_allow_quality_loss);
    REQUIRE(fb.valid);
    REQUIRE(fb.target == texture_format::rg16_float);
    REQUIRE(fb.category == fallback_category::quality_loss);

    fb = resolve_fallback(texture_format::rg16_float, fallback_allow_quality_loss);
    REQUIRE(fb.valid);
    REQUIRE(fb.target == texture_format::rg8_unorm);

    fb = resolve_fallback(texture_format::r16_unorm, fallback_allow_quality_loss);
    REQUIRE(fb.valid);
    REQUIRE(fb.target == texture_format::r8_unorm);

    fb = resolve_fallback(texture_format::rg16_unorm, fallback_allow_quality_loss);
    REQUIRE(fb.valid);
    REQUIRE(fb.target == texture_format::rg8_unorm);

    fb = resolve_fallback(texture_format::rgba16_unorm, fallback_allow_quality_loss);
    REQUIRE(fb.valid);
    REQUIRE(fb.target == texture_format::rgba8_unorm);
}

TEST_CASE("#35: quality-loss returns invalid for formats with no degradation target", "[format_resolve]") {
    auto fb = resolve_fallback(texture_format::depth32_float, fallback_allow_quality_loss);
    REQUIRE_FALSE(fb.valid);

    fb = resolve_fallback(texture_format::r8_unorm, fallback_allow_quality_loss);
    REQUIRE_FALSE(fb.valid);

    fb = resolve_fallback(texture_format::r32_uint, fallback_allow_quality_loss);
    REQUIRE_FALSE(fb.valid);
}

TEST_CASE("#35: category priority — storage_compatible wins over quality_loss for rgba8_unorm", "[format_resolve]") {
    auto fb = resolve_fallback(texture_format::rgba8_unorm,
        fallback_allow_storage_compatible | fallback_allow_quality_loss);
    REQUIRE(fb.valid);
    REQUIRE(fb.target == texture_format::bgra8_unorm);
    REQUIRE(fb.category == fallback_category::storage_compatible);
}

TEST_CASE("#35: category priority — channel_expansion wins over quality_loss for rgb16_float", "[format_resolve]") {
    auto fb = resolve_fallback(texture_format::rgb16_float,
        fallback_allow_channel_expansion | fallback_allow_quality_loss);
    REQUIRE(fb.valid);
    REQUIRE(fb.target == texture_format::rgba16_float);
    REQUIRE(fb.category == fallback_category::channel_expansion);
}

TEST_CASE("#35: category priority — storage_compatible wins over channel_expansion for bgra8_unorm", "[format_resolve]") {
    auto fb = resolve_fallback(texture_format::bgra8_unorm,
        fallback_allow_storage_compatible | fallback_allow_channel_expansion);
    REQUIRE(fb.valid);
    REQUIRE(fb.target == texture_format::rgba8_unorm);
    REQUIRE(fb.category == fallback_category::storage_compatible);
}

TEST_CASE("#35: classify_observed_format rejects quality-loss observed target that skips steps", "[format_resolve]") {
    // requested=rgba32_float, observed=rgba8_unorm (skipped rgba16_float),
    // fallback_target=rgba16_float → observed != target, not storage-compatible → error
    auto r = classify_observed_format(
        texture_format::rgba32_float, texture_format::rgba8_unorm,
        fallback_category::quality_loss, texture_format::rgba16_float,
        fallback_allow_quality_loss);
    REQUIRE_FALSE(r.ok());
}

TEST_CASE("#35: classify_observed_format accepts quality-loss with storage-compatible observed", "[format_resolve]") {
    auto r = classify_observed_format(
        texture_format::rgba16_float, texture_format::bgra8_unorm,
        fallback_category::quality_loss, texture_format::rgba8_unorm,
        fallback_allow_quality_loss | fallback_allow_storage_compatible);
    REQUIRE(r.ok());
    REQUIRE(r.value() == fallback_category::quality_loss);
}

TEST_CASE("#35: resolve_fallback_metadata rejects skipped-step quality-loss (rgba32->rgba8)", "[format_resolve]") {
    auto r = resolve_fallback_metadata(
        texture_format::rgba32_float, texture_format::rgba8_unorm,
        fallback_category::quality_loss, texture_format::rgba16_float);
    REQUIRE_FALSE(r.ok());
}

TEST_CASE("#35: resolve_fallback_metadata rejects skipped-step quality-loss (r32->r8)", "[format_resolve]") {
    auto r = resolve_fallback_metadata(
        texture_format::r32_float, texture_format::r8_unorm,
        fallback_category::quality_loss, texture_format::r16_float);
    REQUIRE_FALSE(r.ok());
}

TEST_CASE("#35: quality-loss disabled — all 32F formats stay with fallback_safe_defaults", "[format_resolve]") {
    auto fb = resolve_fallback(texture_format::r32_float, fallback_safe_defaults);
    REQUIRE_FALSE(fb.valid);

    fb = resolve_fallback(texture_format::rg32_float, fallback_safe_defaults);
    REQUIRE_FALSE(fb.valid);

    fb = resolve_fallback(texture_format::rgba32_float, fallback_safe_defaults);
    REQUIRE_FALSE(fb.valid);
}

// ---------- plan_texture_create (#35 sender attempt plan) ----------

TEST_CASE("#35: plan_texture_create — exact only when no fallback available", "[format_resolve]") {
    auto plan = plan_texture_create(texture_format::r8_unorm, fallback_safe_defaults);
    REQUIRE(plan.primary == texture_format::r8_unorm);
    REQUIRE_FALSE(plan.has_fallback);
}

TEST_CASE("#35: plan_texture_create — exact + fallback when available", "[format_resolve]") {
    auto plan = plan_texture_create(texture_format::rgba8_unorm,
        fallback_allow_storage_compatible);
    REQUIRE(plan.primary == texture_format::rgba8_unorm);
    REQUIRE(plan.has_fallback);
    REQUIRE(plan.fallback == texture_format::bgra8_unorm);
    REQUIRE(plan.fallback_cat == fallback_category::storage_compatible);
}

TEST_CASE("#35: plan_texture_create — quality-loss plan is single-step, no chaining", "[format_resolve]") {
    auto plan = plan_texture_create(texture_format::rgba32_float,
        fallback_allow_quality_loss);
    REQUIRE(plan.primary == texture_format::rgba32_float);
    REQUIRE(plan.has_fallback);
    REQUIRE(plan.fallback == texture_format::rgba16_float);
    REQUIRE(plan.fallback_cat == fallback_category::quality_loss);

    // The plan has exactly two attempts: primary and one fallback.
    // Simulate: primary fails, fallback fails. Sender must stop here.
    // The plan does NOT include rgba8_unorm.
    // Verify by planning the fallback target — it would give rgba8_unorm,
    // but that is a SEPARATE plan for a SEPARATE decision.
    auto second_plan = plan_texture_create(plan.fallback, fallback_allow_quality_loss);
    REQUIRE(second_plan.primary == texture_format::rgba16_float);
    REQUIRE(second_plan.has_fallback);
    REQUIRE(second_plan.fallback == texture_format::rgba8_unorm);

    // The sender must never create this second_plan from the same publish.
    // plan_texture_create is stateless — the contract is enforced by the
    // sender calling it at most once per acquire_writable_frame().
}

TEST_CASE("#35: plan_texture_create — no fallback when quality-loss flag absent", "[format_resolve]") {
    auto plan = plan_texture_create(texture_format::rgba32_float, fallback_safe_defaults);
    REQUIRE(plan.primary == texture_format::rgba32_float);
    REQUIRE_FALSE(plan.has_fallback);
}

TEST_CASE("#35: plan_texture_create — priority selects storage over quality-loss", "[format_resolve]") {
    auto plan = plan_texture_create(texture_format::rgba8_unorm,
        fallback_allow_storage_compatible | fallback_allow_quality_loss);
    REQUIRE(plan.primary == texture_format::rgba8_unorm);
    REQUIRE(plan.has_fallback);
    REQUIRE(plan.fallback == texture_format::bgra8_unorm);
    REQUIRE(plan.fallback_cat == fallback_category::storage_compatible);
}

TEST_CASE("#35: classify_observed_format rejects quality-loss observed without storage flag", "[format_resolve]") {
    auto r = classify_observed_format(
        texture_format::rgba16_float, texture_format::bgra8_unorm,
        fallback_category::quality_loss, texture_format::rgba8_unorm,
        fallback_allow_quality_loss);
    REQUIRE_FALSE(r.ok());
}
