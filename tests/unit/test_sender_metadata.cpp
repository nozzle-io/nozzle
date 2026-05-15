#include <catch2/catch_test_macros.hpp>

#include <nozzle/channel_swizzle.hpp>
#include <nozzle/format_resolve.hpp>
#include <nozzle/texture.hpp>

#include "sender_metadata.hpp"
#include "shared_state.hpp"

using namespace nozzle;

TEST_CASE("write_slot_metadata: writes all fields with identity swizzle", "[metadata]") {
    detail::SenderSharedState::SlotInfo slot{};

    resolved_texture_format resolved{};
    resolved.native.kind = native_format_kind::mtl_pixel_format;
    resolved.native.value = 42;
    resolved.source = format_source::native_observed;
    resolved.native.plane_count = 2;
    resolved.native.plane_strides[0] = 7680;
    resolved.native.plane_strides[1] = 7680;
    resolved.native.plane_offsets[0] = 0;
    resolved.native.plane_offsets[1] = 3110400;

    auto fb = resolve_format_fallback_info(
        texture_format::rgba8_unorm, texture_format::rgba8_unorm,
        fallback_category::none, texture_format::unknown);
    REQUIRE(fb.ok());

    detail::write_slot_metadata(slot, 100, 999, 1920, 1080,
        resolved, fb.value());

    REQUIRE(slot.frame_number == 100);
    REQUIRE(slot.shared_resource_id == 999);
    REQUIRE(slot.width == 1920);
    REQUIRE(slot.height == 1080);
    REQUIRE(slot.format == static_cast<uint32_t>(texture_format::rgba8_unorm));
    REQUIRE(slot.semantic_format == static_cast<uint32_t>(texture_format::rgba8_unorm));
    REQUIRE(slot.channel_swizzle == static_cast<uint8_t>(channel_swizzle::identity));
    REQUIRE(slot.fallback_target == static_cast<uint32_t>(texture_format::unknown));
    REQUIRE(slot.fallback_category == static_cast<uint8_t>(fallback_category::none));
    REQUIRE(slot.fallback_quality_loss == 0);
    REQUIRE(slot.native_format_kind == static_cast<uint8_t>(native_format_kind::mtl_pixel_format));
    REQUIRE(slot.format_source == static_cast<uint8_t>(format_source::native_observed));
    REQUIRE(slot.native_format_value == 42);
    REQUIRE(slot.plane_count == 2);
    REQUIRE(slot.plane_strides[0] == 7680);
    REQUIRE(slot.plane_strides[1] == 7680);
    REQUIRE(slot.plane_offsets[0] == 0);
    REQUIRE(slot.plane_offsets[1] == 3110400);
}

TEST_CASE("write_slot_metadata: swizzle from fallback info", "[metadata]") {
    detail::SenderSharedState::SlotInfo slot{};

    resolved_texture_format resolved{};
    auto fb = resolve_format_fallback_info(
        texture_format::rgba8_unorm, texture_format::bgra8_unorm,
        fallback_category::storage_compatible, texture_format::bgra8_unorm);
    REQUIRE(fb.ok());
    REQUIRE(fb.value().swizzle == channel_swizzle::swap_rb);

    detail::write_slot_metadata(slot, 1, 1, 640, 480,
        resolved, fb.value());

    REQUIRE(slot.format == static_cast<uint32_t>(texture_format::bgra8_unorm));
    REQUIRE(slot.semantic_format == static_cast<uint32_t>(texture_format::rgba8_unorm));
    REQUIRE(slot.channel_swizzle == static_cast<uint8_t>(channel_swizzle::swap_rb));
    REQUIRE(slot.fallback_category == static_cast<uint8_t>(fallback_category::storage_compatible));
    REQUIRE(slot.fallback_target == static_cast<uint32_t>(texture_format::bgra8_unorm));
}

TEST_CASE("write_slot_metadata: plane_count 0 skips plane arrays", "[metadata]") {
    detail::SenderSharedState::SlotInfo slot{};

    resolved_texture_format resolved{};
    resolved.native.plane_count = 0;

    auto fb = resolve_format_fallback_info(
        texture_format::r8_unorm, texture_format::r8_unorm,
        fallback_category::none, texture_format::unknown);
    REQUIRE(fb.ok());

    detail::write_slot_metadata(slot, 1, 1, 100, 100,
        resolved, fb.value());

    REQUIRE(slot.plane_count == 0);
    REQUIRE(slot.plane_strides[0] == 0);
    REQUIRE(slot.plane_offsets[0] == 0);
}

TEST_CASE("write_global_metadata: writes all fields with identity swizzle", "[metadata]") {
    detail::SenderSharedState state{};

    auto fb = resolve_format_fallback_info(
        texture_format::rgba16_float, texture_format::rgba16_float,
        fallback_category::none, texture_format::unknown);
    REQUIRE(fb.ok());

    detail::write_global_metadata(state, 3840, 2160, fb.value());

    REQUIRE(state.width == 3840);
    REQUIRE(state.height == 2160);
    REQUIRE(state.format == static_cast<uint32_t>(texture_format::rgba16_float));
    REQUIRE(state.semantic_format == static_cast<uint32_t>(texture_format::rgba16_float));
    REQUIRE(state.channel_swizzle == static_cast<uint8_t>(channel_swizzle::identity));
    REQUIRE(state.fallback_target == static_cast<uint32_t>(texture_format::unknown));
    REQUIRE(state.fallback_category == static_cast<uint8_t>(fallback_category::none));
    REQUIRE(state.fallback_quality_loss == 0);
}

TEST_CASE("write_global_metadata: fallback then exact resets fallback fields", "[metadata][stale]") {
    detail::SenderSharedState state{};

    auto fallback_fb = resolve_format_fallback_info(
        texture_format::rgba8_unorm, texture_format::bgra8_unorm,
        fallback_category::storage_compatible, texture_format::bgra8_unorm);
    REQUIRE(fallback_fb.ok());
    detail::write_global_metadata(state, 640, 480, fallback_fb.value());

    REQUIRE(state.channel_swizzle == static_cast<uint8_t>(channel_swizzle::swap_rb));
    REQUIRE(state.fallback_category == static_cast<uint8_t>(fallback_category::storage_compatible));

    auto exact_fb = resolve_format_fallback_info(
        texture_format::rgba16_float, texture_format::rgba16_float,
        fallback_category::none, texture_format::unknown);
    REQUIRE(exact_fb.ok());
    detail::write_global_metadata(state, 1920, 1080, exact_fb.value());

    REQUIRE(state.channel_swizzle == static_cast<uint8_t>(channel_swizzle::identity));
    REQUIRE(state.fallback_target == static_cast<uint32_t>(texture_format::unknown));
    REQUIRE(state.fallback_category == static_cast<uint8_t>(fallback_category::none));
    REQUIRE(state.fallback_quality_loss == 0);
    REQUIRE(state.format == static_cast<uint32_t>(texture_format::rgba16_float));
    REQUIRE(state.semantic_format == static_cast<uint32_t>(texture_format::rgba16_float));
}

TEST_CASE("write_slot_metadata: fallback then exact resets fallback fields", "[metadata][stale]") {
    detail::SenderSharedState::SlotInfo slot{};
    resolved_texture_format resolved{};

    auto fallback_fb = resolve_format_fallback_info(
        texture_format::rgba8_unorm, texture_format::bgra8_unorm,
        fallback_category::storage_compatible, texture_format::bgra8_unorm);
    REQUIRE(fallback_fb.ok());
    detail::write_slot_metadata(slot, 1, 100, 640, 480, resolved, fallback_fb.value());

    REQUIRE(slot.channel_swizzle == static_cast<uint8_t>(channel_swizzle::swap_rb));
    REQUIRE(slot.fallback_category == static_cast<uint8_t>(fallback_category::storage_compatible));

    auto exact_fb = resolve_format_fallback_info(
        texture_format::rgba16_float, texture_format::rgba16_float,
        fallback_category::none, texture_format::unknown);
    REQUIRE(exact_fb.ok());
    detail::write_slot_metadata(slot, 2, 200, 1920, 1080, resolved, exact_fb.value());

    REQUIRE(slot.channel_swizzle == static_cast<uint8_t>(channel_swizzle::identity));
    REQUIRE(slot.fallback_target == static_cast<uint32_t>(texture_format::unknown));
    REQUIRE(slot.fallback_category == static_cast<uint8_t>(fallback_category::none));
    REQUIRE(slot.fallback_quality_loss == 0);
    REQUIRE(slot.format == static_cast<uint32_t>(texture_format::rgba16_float));
}

TEST_CASE("write_global_metadata: external texture clears stale fallback from previous frame", "[metadata][stale]") {
    detail::SenderSharedState state{};

    auto fallback_fb = resolve_format_fallback_info(
        texture_format::rgba8_unorm, texture_format::bgra8_unorm,
        fallback_category::storage_compatible, texture_format::bgra8_unorm);
    REQUIRE(fallback_fb.ok());
    detail::write_global_metadata(state, 640, 480, fallback_fb.value());

    REQUIRE(state.channel_swizzle != static_cast<uint8_t>(channel_swizzle::identity));

    format_fallback_info ext_fb;
    ext_fb.storage_format = texture_format::rgba8_unorm;
    ext_fb.requested_format = texture_format::rgba8_unorm;
    ext_fb.swizzle = channel_swizzle::identity;
    ext_fb.category = fallback_category::none;
    ext_fb.fallback_target = texture_format::unknown;
    ext_fb.quality_loss = false;
    detail::write_global_metadata(state, 800, 600, ext_fb);

    REQUIRE(state.channel_swizzle == static_cast<uint8_t>(channel_swizzle::identity));
    REQUIRE(state.fallback_target == static_cast<uint32_t>(texture_format::unknown));
    REQUIRE(state.fallback_category == static_cast<uint8_t>(fallback_category::none));
    REQUIRE(state.fallback_quality_loss == 0);
}
