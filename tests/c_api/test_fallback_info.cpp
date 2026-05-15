// nozzle - test_c_api_fallback_info.cpp - C API format_fallback_info getter tests

#include <catch2/catch_test_macros.hpp>

#include <nozzle/nozzle_c.h>
#include <nozzle/types.hpp>
#include <nozzle/frame.hpp>

#include "nozzle_c_types.hpp"

// ========== Null-pointer validation ==========

TEST_CASE("C API: nozzle_frame_get_format_fallback_info null frame", "[c_api][fallback]") {
    NozzleFormatFallbackInfo info{};
    REQUIRE(nozzle_frame_get_format_fallback_info(nullptr, &info) == NOZZLE_ERROR_INVALID_ARGUMENT);
}

TEST_CASE("C API: nozzle_frame_get_format_fallback_info null out", "[c_api][fallback]") {
    NozzleFrame f{};
    REQUIRE(nozzle_frame_get_format_fallback_info(&f, nullptr) == NOZZLE_ERROR_INVALID_ARGUMENT);
}

TEST_CASE("C API: nozzle_receiver_get_connected_format_fallback_info null receiver", "[c_api][fallback]") {
    NozzleFormatFallbackInfo info{};
    REQUIRE(nozzle_receiver_get_connected_format_fallback_info(nullptr, &info) == NOZZLE_ERROR_INVALID_ARGUMENT);
}

TEST_CASE("C API: nozzle_receiver_get_connected_format_fallback_info null out", "[c_api][fallback]") {
    NozzleReceiver r{};
    REQUIRE(nozzle_receiver_get_connected_format_fallback_info(&r, nullptr) == NOZZLE_ERROR_INVALID_ARGUMENT);
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

// ========== Writable frame getter: returns default fallback ==========

TEST_CASE("getter: writable frame returns default fallback", "[c_api][fallback]") {
    NozzleFrame f{};
    f.is_writable = true;

    NozzleFormatFallbackInfo out{};
    REQUIRE(nozzle_frame_get_format_fallback_info(&f, &out) == NOZZLE_OK);

    CHECK(out.requested_format == NOZZLE_FORMAT_UNKNOWN);
    CHECK(out.storage_format == NOZZLE_FORMAT_UNKNOWN);
    CHECK(out.fallback_target == NOZZLE_FORMAT_UNKNOWN);
    CHECK(out.category == NOZZLE_FALLBACK_CATEGORY_NONE);
    CHECK(out.swizzle == NOZZLE_CHANNEL_SWIZZLE_IDENTITY);
    CHECK(out.quality_loss == 0);
}

// ========== Readable frame getter: returns frame_info.fallback ==========

TEST_CASE("getter: readable frame returns frame_info.fallback (exact match)", "[c_api][fallback]") {
    nozzle::format_fallback_info fb;
    fb.requested_format = nozzle::texture_format::rgba8_unorm;
    fb.storage_format = nozzle::texture_format::rgba8_unorm;
    fb.fallback_target = nozzle::texture_format::unknown;
    fb.category = nozzle::fallback_category::none;
    fb.swizzle = nozzle::channel_swizzle::identity;
    fb.quality_loss = false;

    nozzle::frame_info fi{};
    fi.fallback = fb;

    NozzleFrame f{};
    f.obj = std::make_unique<nozzle::frame>(nozzle::detail::make_frame(nozzle::texture{}, fi));
    f.is_writable = false;

    NozzleFormatFallbackInfo out{};
    REQUIRE(nozzle_frame_get_format_fallback_info(&f, &out) == NOZZLE_OK);

    CHECK(out.requested_format == NOZZLE_FORMAT_RGBA8_UNORM);
    CHECK(out.storage_format == NOZZLE_FORMAT_RGBA8_UNORM);
    CHECK(out.fallback_target == NOZZLE_FORMAT_UNKNOWN);
    CHECK(out.category == NOZZLE_FALLBACK_CATEGORY_NONE);
    CHECK(out.swizzle == NOZZLE_CHANNEL_SWIZZLE_IDENTITY);
    CHECK(out.quality_loss == 0);
}

TEST_CASE("getter: readable frame with quality_loss fallback", "[c_api][fallback]") {
    nozzle::format_fallback_info fb;
    fb.requested_format = nozzle::texture_format::rgba32_float;
    fb.storage_format = nozzle::texture_format::rgba16_float;
    fb.fallback_target = nozzle::texture_format::rgba16_float;
    fb.category = nozzle::fallback_category::quality_loss;
    fb.swizzle = nozzle::channel_swizzle::identity;
    fb.quality_loss = true;

    nozzle::frame_info fi{};
    fi.fallback = fb;

    NozzleFrame f{};
    f.obj = std::make_unique<nozzle::frame>(nozzle::detail::make_frame(nozzle::texture{}, fi));
    f.is_writable = false;

    NozzleFormatFallbackInfo out{};
    REQUIRE(nozzle_frame_get_format_fallback_info(&f, &out) == NOZZLE_OK);

    CHECK(out.requested_format == NOZZLE_FORMAT_RGBA32_FLOAT);
    CHECK(out.storage_format == NOZZLE_FORMAT_RGBA16_FLOAT);
    CHECK(out.fallback_target == NOZZLE_FORMAT_RGBA16_FLOAT);
    CHECK(out.category == NOZZLE_FALLBACK_CATEGORY_QUALITY_LOSS);
    CHECK(out.swizzle == NOZZLE_CHANNEL_SWIZZLE_IDENTITY);
    CHECK(out.quality_loss == 1);
}

// ========== Edge case: category=none, swizzle=swap_rb, requested!=storage ==========

TEST_CASE("getter: external edge case (category=none + swap_rb + requested!=storage)", "[c_api][fallback]") {
    nozzle::format_fallback_info fb;
    fb.requested_format = nozzle::texture_format::bgra8_unorm;
    fb.storage_format = nozzle::texture_format::rgba8_unorm;
    fb.fallback_target = nozzle::texture_format::unknown;
    fb.category = nozzle::fallback_category::none;
    fb.swizzle = nozzle::channel_swizzle::swap_rb;
    fb.quality_loss = false;

    nozzle::frame_info fi{};
    fi.fallback = fb;

    NozzleFrame f{};
    f.obj = std::make_unique<nozzle::frame>(nozzle::detail::make_frame(nozzle::texture{}, fi));
    f.is_writable = false;

    NozzleFormatFallbackInfo out{};
    REQUIRE(nozzle_frame_get_format_fallback_info(&f, &out) == NOZZLE_OK);

    CHECK(out.requested_format == NOZZLE_FORMAT_BGRA8_UNORM);
    CHECK(out.storage_format == NOZZLE_FORMAT_RGBA8_UNORM);
    CHECK(out.fallback_target == NOZZLE_FORMAT_UNKNOWN);
    CHECK(out.category == NOZZLE_FALLBACK_CATEGORY_NONE);
    CHECK(out.swizzle == NOZZLE_CHANNEL_SWIZZLE_SWAP_RB);
    CHECK(out.quality_loss == 0);
}

TEST_CASE("getter: channel_expansion with swap_rb", "[c_api][fallback]") {
    nozzle::format_fallback_info fb;
    fb.requested_format = nozzle::texture_format::bgra8_unorm;
    fb.storage_format = nozzle::texture_format::rgba8_unorm;
    fb.fallback_target = nozzle::texture_format::rgba8_unorm;
    fb.category = nozzle::fallback_category::channel_expansion;
    fb.swizzle = nozzle::channel_swizzle::swap_rb;
    fb.quality_loss = false;

    nozzle::frame_info fi{};
    fi.fallback = fb;

    NozzleFrame f{};
    f.obj = std::make_unique<nozzle::frame>(nozzle::detail::make_frame(nozzle::texture{}, fi));
    f.is_writable = false;

    NozzleFormatFallbackInfo out{};
    REQUIRE(nozzle_frame_get_format_fallback_info(&f, &out) == NOZZLE_OK);

    CHECK(out.requested_format == NOZZLE_FORMAT_BGRA8_UNORM);
    CHECK(out.storage_format == NOZZLE_FORMAT_RGBA8_UNORM);
    CHECK(out.fallback_target == NOZZLE_FORMAT_RGBA8_UNORM);
    CHECK(out.category == NOZZLE_FALLBACK_CATEGORY_CHANNEL_EXPANSION);
    CHECK(out.swizzle == NOZZLE_CHANNEL_SWIZZLE_SWAP_RB);
    CHECK(out.quality_loss == 0);
}

// ========== Receiver getter: returns connected_sender_info.fallback ==========

namespace {

NozzleReceiver make_test_receiver_with_fallback(const nozzle::format_fallback_info &fb) {
    NozzleReceiver r{};
    r.use_cached_connected_info_for_tests = true;
    nozzle::connected_sender_info ci{};
    ci.fallback = fb;
    r.cached_connected_info = ci;
    return r;
}

} // anonymous namespace

TEST_CASE("getter: receiver returns quality_loss fallback", "[c_api][fallback]") {
    nozzle::format_fallback_info fb;
    fb.requested_format = nozzle::texture_format::rgba32_float;
    fb.storage_format = nozzle::texture_format::rgba16_float;
    fb.fallback_target = nozzle::texture_format::rgba16_float;
    fb.category = nozzle::fallback_category::quality_loss;
    fb.swizzle = nozzle::channel_swizzle::identity;
    fb.quality_loss = true;

    auto r = make_test_receiver_with_fallback(fb);

    NozzleFormatFallbackInfo out{};
    REQUIRE(nozzle_receiver_get_connected_format_fallback_info(&r, &out) == NOZZLE_OK);

    CHECK(out.requested_format == NOZZLE_FORMAT_RGBA32_FLOAT);
    CHECK(out.storage_format == NOZZLE_FORMAT_RGBA16_FLOAT);
    CHECK(out.fallback_target == NOZZLE_FORMAT_RGBA16_FLOAT);
    CHECK(out.category == NOZZLE_FALLBACK_CATEGORY_QUALITY_LOSS);
    CHECK(out.swizzle == NOZZLE_CHANNEL_SWIZZLE_IDENTITY);
    CHECK(out.quality_loss == 1);
}

TEST_CASE("getter: receiver external edge case (category=none + swap_rb)", "[c_api][fallback]") {
    nozzle::format_fallback_info fb;
    fb.requested_format = nozzle::texture_format::bgra8_unorm;
    fb.storage_format = nozzle::texture_format::rgba8_unorm;
    fb.fallback_target = nozzle::texture_format::unknown;
    fb.category = nozzle::fallback_category::none;
    fb.swizzle = nozzle::channel_swizzle::swap_rb;
    fb.quality_loss = false;

    auto r = make_test_receiver_with_fallback(fb);

    NozzleFormatFallbackInfo out{};
    REQUIRE(nozzle_receiver_get_connected_format_fallback_info(&r, &out) == NOZZLE_OK);

    CHECK(out.requested_format == NOZZLE_FORMAT_BGRA8_UNORM);
    CHECK(out.storage_format == NOZZLE_FORMAT_RGBA8_UNORM);
    CHECK(out.fallback_target == NOZZLE_FORMAT_UNKNOWN);
    CHECK(out.category == NOZZLE_FALLBACK_CATEGORY_NONE);
    CHECK(out.swizzle == NOZZLE_CHANNEL_SWIZZLE_SWAP_RB);
    CHECK(out.quality_loss == 0);
}

TEST_CASE("getter: receiver storage_compatible with swap_rb", "[c_api][fallback]") {
    nozzle::format_fallback_info fb;
    fb.requested_format = nozzle::texture_format::rgba8_srgb;
    fb.storage_format = nozzle::texture_format::bgra8_unorm;
    fb.fallback_target = nozzle::texture_format::bgra8_unorm;
    fb.category = nozzle::fallback_category::storage_compatible;
    fb.swizzle = nozzle::channel_swizzle::swap_rb;
    fb.quality_loss = false;

    auto r = make_test_receiver_with_fallback(fb);

    NozzleFormatFallbackInfo out{};
    REQUIRE(nozzle_receiver_get_connected_format_fallback_info(&r, &out) == NOZZLE_OK);

    CHECK(out.requested_format == NOZZLE_FORMAT_RGBA8_SRGB);
    CHECK(out.storage_format == NOZZLE_FORMAT_BGRA8_UNORM);
    CHECK(out.fallback_target == NOZZLE_FORMAT_BGRA8_UNORM);
    CHECK(out.category == NOZZLE_FALLBACK_CATEGORY_STORAGE_COMPATIBLE);
    CHECK(out.swizzle == NOZZLE_CHANNEL_SWIZZLE_SWAP_RB);
    CHECK(out.quality_loss == 0);
}
