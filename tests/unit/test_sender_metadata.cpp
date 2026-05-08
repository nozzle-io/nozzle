#include <catch2/catch_test_macros.hpp>

#include <nozzle/channel_swizzle.hpp>
#include <nozzle/format_resolve.hpp>
#include <nozzle/texture.hpp>

#include "sender_detail.hpp"
#include "shared_state.hpp"

using namespace nozzle;

TEST_CASE("write_slot_metadata: storage == semantic writes identical values", "[metadata]") {
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

    detail::write_slot_metadata(slot, 100, 999, 1920, 1080,
        texture_format::rgba8_unorm, texture_format::rgba8_unorm, resolved);

    REQUIRE(slot.frame_number == 100);
    REQUIRE(slot.shared_resource_id == 999);
    REQUIRE(slot.width == 1920);
    REQUIRE(slot.height == 1080);
    REQUIRE(slot.format == static_cast<uint32_t>(texture_format::rgba8_unorm));
    REQUIRE(slot.semantic_format == static_cast<uint32_t>(texture_format::rgba8_unorm));
    REQUIRE(slot.channel_swizzle == static_cast<uint8_t>(channel_swizzle::identity));
    REQUIRE(slot.native_format_kind == static_cast<uint8_t>(native_format_kind::mtl_pixel_format));
    REQUIRE(slot.format_source == static_cast<uint8_t>(format_source::native_observed));
    REQUIRE(slot.native_format_value == 42);
    REQUIRE(slot.plane_count == 2);
    REQUIRE(slot.plane_strides[0] == 7680);
    REQUIRE(slot.plane_strides[1] == 7680);
    REQUIRE(slot.plane_offsets[0] == 0);
    REQUIRE(slot.plane_offsets[1] == 3110400);
}

TEST_CASE("write_slot_metadata: storage != semantic preserves distinction", "[metadata]") {
    detail::SenderSharedState::SlotInfo slot{};

    resolved_texture_format resolved{};
    resolved.native.kind = native_format_kind::dxgi_format;
    resolved.native.value = 87;
    resolved.source = format_source::caller_hint;

    detail::write_slot_metadata(slot, 200, 888, 640, 480,
        texture_format::bgra8_unorm, texture_format::rgba8_unorm, resolved);

    REQUIRE(slot.format == static_cast<uint32_t>(texture_format::bgra8_unorm));
    REQUIRE(slot.semantic_format == static_cast<uint32_t>(texture_format::rgba8_unorm));
    REQUIRE(slot.format != slot.semantic_format);
}

TEST_CASE("write_slot_metadata: plane_count 0 skips plane arrays", "[metadata]") {
    detail::SenderSharedState::SlotInfo slot{};

    resolved_texture_format resolved{};
    resolved.native.plane_count = 0;

    detail::write_slot_metadata(slot, 1, 1, 100, 100,
        texture_format::r8_unorm, texture_format::r8_unorm, resolved);

    REQUIRE(slot.plane_count == 0);
    REQUIRE(slot.plane_strides[0] == 0);
    REQUIRE(slot.plane_offsets[0] == 0);
}

TEST_CASE("write_global_metadata: writes all fields", "[metadata]") {
    detail::SenderSharedState state{};

    detail::write_global_metadata(state, 3840, 2160,
        texture_format::rgba16_float, texture_format::rgba8_unorm);

    REQUIRE(state.width == 3840);
    REQUIRE(state.height == 2160);
    REQUIRE(state.format == static_cast<uint32_t>(texture_format::rgba16_float));
    REQUIRE(state.semantic_format == static_cast<uint32_t>(texture_format::rgba8_unorm));
    REQUIRE(state.channel_swizzle == static_cast<uint8_t>(channel_swizzle::identity));
}

TEST_CASE("write_global_metadata: storage == semantic is identity", "[metadata]") {
    detail::SenderSharedState state{};

    detail::write_global_metadata(state, 800, 600,
        texture_format::r32_float, texture_format::r32_float);

    REQUIRE(state.format == state.semantic_format);
}
