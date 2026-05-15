// nozzle - test_c_api_fallback_info.cpp - C API format_fallback_info null/enum/edge tests

#include <catch2/catch_test_macros.hpp>

#include <nozzle/nozzle_c.h>
#include <nozzle/types.hpp>

// ========== Null-pointer validation ==========

TEST_CASE("C API: nozzle_frame_get_format_fallback_info null frame", "[c_api][fallback]") {
    NozzleFormatFallbackInfo info{};
    REQUIRE(nozzle_frame_get_format_fallback_info(nullptr, &info) == NOZZLE_ERROR_INVALID_ARGUMENT);
}

TEST_CASE("C API: nozzle_frame_get_format_fallback_info null out", "[c_api][fallback]") {
    REQUIRE(nozzle_frame_get_format_fallback_info(
        reinterpret_cast<NozzleFrame *>(1), nullptr) == NOZZLE_ERROR_INVALID_ARGUMENT);
}

TEST_CASE("C API: nozzle_receiver_get_connected_format_fallback_info null receiver", "[c_api][fallback]") {
    NozzleFormatFallbackInfo info{};
    REQUIRE(nozzle_receiver_get_connected_format_fallback_info(nullptr, &info) == NOZZLE_ERROR_INVALID_ARGUMENT);
}

TEST_CASE("C API: nozzle_receiver_get_connected_format_fallback_info null out", "[c_api][fallback]") {
    REQUIRE(nozzle_receiver_get_connected_format_fallback_info(
        reinterpret_cast<NozzleReceiver *>(1), nullptr) == NOZZLE_ERROR_INVALID_ARGUMENT);
}

// ========== Enum value mapping ==========

TEST_CASE("C API: fallback_category enum values match C++", "[c_api][fallback]") {
    CHECK(NOZZLE_FALLBACK_CATEGORY_NONE == static_cast<int>(nozzle::fallback_category::none));
    CHECK(NOZZLE_FALLBACK_CATEGORY_STORAGE_COMPATIBLE == static_cast<int>(nozzle::fallback_category::storage_compatible));
    CHECK(NOZZLE_FALLBACK_CATEGORY_CHANNEL_EXPANSION == static_cast<int>(nozzle::fallback_category::channel_expansion));
    CHECK(NOZZLE_FALLBACK_CATEGORY_QUALITY_LOSS == static_cast<int>(nozzle::fallback_category::quality_loss));
}

TEST_CASE("C API: channel_swizzle enum values match C++", "[c_api][fallback]") {
    CHECK(NOZZLE_CHANNEL_SWIZZLE_IDENTITY == static_cast<int>(nozzle::channel_swizzle::identity));
    CHECK(NOZZLE_CHANNEL_SWIZZLE_SWAP_RB == static_cast<int>(nozzle::channel_swizzle::swap_rb));
}

// ========== Edge case: category=none, swizzle=swap_rb, requested!=storage ==========

TEST_CASE("C API: category=none + swizzle=swap_rb + requested!=storage preserved", "[c_api][fallback]") {
    nozzle::format_fallback_info fb;
    fb.requested_format = nozzle::texture_format::bgra8_unorm;
    fb.storage_format = nozzle::texture_format::rgba8_unorm;
    fb.fallback_target = nozzle::texture_format::unknown;
    fb.category = nozzle::fallback_category::none;
    fb.swizzle = nozzle::channel_swizzle::swap_rb;
    fb.quality_loss = false;

    nozzle::frame_info fi{};
    fi.fallback = fb;

    CHECK(fi.fallback.category == nozzle::fallback_category::none);
    CHECK(fi.fallback.swizzle == nozzle::channel_swizzle::swap_rb);
    CHECK(fi.fallback.requested_format != fi.fallback.storage_format);

    CHECK(static_cast<NozzleFallbackCategory>(fi.fallback.category) == NOZZLE_FALLBACK_CATEGORY_NONE);
    CHECK(static_cast<NozzleChannelSwizzle>(fi.fallback.swizzle) == NOZZLE_CHANNEL_SWIZZLE_SWAP_RB);
}
