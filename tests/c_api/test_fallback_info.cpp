// nozzle - test_c_api_fallback_info.cpp - C API format_fallback_info conversion tests

#include <catch2/catch_test_macros.hpp>

#include <nozzle/nozzle_c.h>
#include <nozzle/types.hpp>
#include "nozzle_c_detail.hpp"

// ========== Null-pointer validation (getter boundary) ==========

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

// ========== Conversion helper: normal path ==========

TEST_CASE("helper: exact match rgba8_unorm", "[c_api][fallback]") {
    nozzle::format_fallback_info fb;
    fb.requested_format = nozzle::texture_format::rgba8_unorm;
    fb.storage_format = nozzle::texture_format::rgba8_unorm;
    fb.fallback_target = nozzle::texture_format::unknown;
    fb.category = nozzle::fallback_category::none;
    fb.swizzle = nozzle::channel_swizzle::identity;
    fb.quality_loss = false;

    NozzleFormatFallbackInfo out{};
    REQUIRE(nozzle_c_detail_fill_fallback(&out, fb) == NOZZLE_OK);

    CHECK(out.requested_format == NOZZLE_FORMAT_RGBA8_UNORM);
    CHECK(out.storage_format == NOZZLE_FORMAT_RGBA8_UNORM);
    CHECK(out.fallback_target == NOZZLE_FORMAT_UNKNOWN);
    CHECK(out.category == NOZZLE_FALLBACK_CATEGORY_NONE);
    CHECK(out.swizzle == NOZZLE_CHANNEL_SWIZZLE_IDENTITY);
    CHECK(out.quality_loss == 0);
}

TEST_CASE("helper: storage_compatible with quality_loss", "[c_api][fallback]") {
    nozzle::format_fallback_info fb;
    fb.requested_format = nozzle::texture_format::rgba8_srgb;
    fb.storage_format = nozzle::texture_format::rgba8_unorm;
    fb.fallback_target = nozzle::texture_format::rgba8_unorm;
    fb.category = nozzle::fallback_category::storage_compatible;
    fb.swizzle = nozzle::channel_swizzle::identity;
    fb.quality_loss = true;

    NozzleFormatFallbackInfo out{};
    REQUIRE(nozzle_c_detail_fill_fallback(&out, fb) == NOZZLE_OK);

    CHECK(out.requested_format == NOZZLE_FORMAT_RGBA8_SRGB);
    CHECK(out.storage_format == NOZZLE_FORMAT_RGBA8_UNORM);
    CHECK(out.fallback_target == NOZZLE_FORMAT_RGBA8_UNORM);
    CHECK(out.category == NOZZLE_FALLBACK_CATEGORY_STORAGE_COMPATIBLE);
    CHECK(out.swizzle == NOZZLE_CHANNEL_SWIZZLE_IDENTITY);
    CHECK(out.quality_loss == 1);
}

TEST_CASE("helper: channel_expansion + swap_rb", "[c_api][fallback]") {
    nozzle::format_fallback_info fb;
    fb.requested_format = nozzle::texture_format::bgra8_unorm;
    fb.storage_format = nozzle::texture_format::rgba8_unorm;
    fb.fallback_target = nozzle::texture_format::rgba8_unorm;
    fb.category = nozzle::fallback_category::channel_expansion;
    fb.swizzle = nozzle::channel_swizzle::swap_rb;
    fb.quality_loss = false;

    NozzleFormatFallbackInfo out{};
    REQUIRE(nozzle_c_detail_fill_fallback(&out, fb) == NOZZLE_OK);

    CHECK(out.requested_format == NOZZLE_FORMAT_BGRA8_UNORM);
    CHECK(out.storage_format == NOZZLE_FORMAT_RGBA8_UNORM);
    CHECK(out.fallback_target == NOZZLE_FORMAT_RGBA8_UNORM);
    CHECK(out.category == NOZZLE_FALLBACK_CATEGORY_CHANNEL_EXPANSION);
    CHECK(out.swizzle == NOZZLE_CHANNEL_SWIZZLE_SWAP_RB);
    CHECK(out.quality_loss == 0);
}

// ========== Conversion helper: external edge case ==========

TEST_CASE("helper: category=none + swizzle=swap_rb + requested!=storage", "[c_api][fallback]") {
    nozzle::format_fallback_info fb;
    fb.requested_format = nozzle::texture_format::bgra8_unorm;
    fb.storage_format = nozzle::texture_format::rgba8_unorm;
    fb.fallback_target = nozzle::texture_format::unknown;
    fb.category = nozzle::fallback_category::none;
    fb.swizzle = nozzle::channel_swizzle::swap_rb;
    fb.quality_loss = false;

    NozzleFormatFallbackInfo out{};
    REQUIRE(nozzle_c_detail_fill_fallback(&out, fb) == NOZZLE_OK);

    CHECK(out.requested_format == NOZZLE_FORMAT_BGRA8_UNORM);
    CHECK(out.storage_format == NOZZLE_FORMAT_RGBA8_UNORM);
    CHECK(out.fallback_target == NOZZLE_FORMAT_UNKNOWN);
    CHECK(out.category == NOZZLE_FALLBACK_CATEGORY_NONE);
    CHECK(out.swizzle == NOZZLE_CHANNEL_SWIZZLE_SWAP_RB);
    CHECK(out.quality_loss == 0);
}

TEST_CASE("helper: default-constructed fallback_info", "[c_api][fallback]") {
    nozzle::format_fallback_info fb{};

    NozzleFormatFallbackInfo out{};
    REQUIRE(nozzle_c_detail_fill_fallback(&out, fb) == NOZZLE_OK);

    CHECK(out.requested_format == NOZZLE_FORMAT_UNKNOWN);
    CHECK(out.storage_format == NOZZLE_FORMAT_UNKNOWN);
    CHECK(out.fallback_target == NOZZLE_FORMAT_UNKNOWN);
    CHECK(out.category == NOZZLE_FALLBACK_CATEGORY_NONE);
    CHECK(out.swizzle == NOZZLE_CHANNEL_SWIZZLE_IDENTITY);
    CHECK(out.quality_loss == 0);
}

TEST_CASE("helper: null out returns INVALID_ARGUMENT", "[c_api][fallback]") {
    nozzle::format_fallback_info fb{};
    REQUIRE(nozzle_c_detail_fill_fallback(nullptr, fb) == NOZZLE_ERROR_INVALID_ARGUMENT);
}
