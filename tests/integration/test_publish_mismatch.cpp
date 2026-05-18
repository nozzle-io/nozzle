#include <catch2/catch_test_macros.hpp>

#include <nozzle/sender.hpp>
#include <nozzle/types.hpp>

static bool is_backend_unavailable(const nozzle::Error &error) {
    return error.code == nozzle::ErrorCode::UnsupportedBackend
        || error.code == nozzle::ErrorCode::ResourceCreationFailed
        || error.code == nozzle::ErrorCode::BackendError;
}

TEST_CASE("publish_native_texture: storage/semantic mismatch rejected before backend access", "[publish]") {
    nozzle::sender_desc desc{};
    desc.name = "test_mismatch_policy";
    desc.application_name = "test_app";

    auto sender_result = nozzle::sender::create(desc);
    if (!sender_result.ok() && is_backend_unavailable(sender_result.error())) {
        SKIP("backend device is not available on this runner");
    }
    REQUIRE(sender_result.ok());
    auto snd = std::move(sender_result.value());

    void *dummy_native = reinterpret_cast<void *>(0x1);

    auto r = snd.publish_native_texture(
        dummy_native, 640, 480,
        nozzle::texture_format::bgra8_unorm,
        nozzle::texture_format::rgba8_unorm);

    REQUIRE_FALSE(r.ok());
    REQUIRE(r.error().code == nozzle::ErrorCode::UnsupportedFormat);
}

TEST_CASE("publish_native_texture: matching storage/semantic passes mismatch check", "[publish]") {
    nozzle::sender_desc desc{};
    desc.name = "test_exact_match";
    desc.application_name = "test_app";

    auto sender_result = nozzle::sender::create(desc);
    if (!sender_result.ok() && is_backend_unavailable(sender_result.error())) {
        SKIP("backend device is not available on this runner");
    }
    REQUIRE(sender_result.ok());
    auto snd = std::move(sender_result.value());

    auto r = snd.publish_native_texture(
        nullptr, 640, 480,
        nozzle::texture_format::rgba8_unorm,
        nozzle::texture_format::rgba8_unorm);

    REQUIRE(r.error().code == nozzle::ErrorCode::InvalidArgument);
}

TEST_CASE("publish_native_texture: 4-arg overload passes mismatch check", "[publish]") {
    nozzle::sender_desc desc{};
    desc.name = "test_4arg_compat";
    desc.application_name = "test_app";

    auto sender_result = nozzle::sender::create(desc);
    if (!sender_result.ok() && is_backend_unavailable(sender_result.error())) {
        SKIP("backend device is not available on this runner");
    }
    REQUIRE(sender_result.ok());
    auto snd = std::move(sender_result.value());

    auto r = snd.publish_native_texture(
        nullptr, 320, 240,
        nozzle::texture_format::rgba8_unorm);

    REQUIRE(r.error().code == nozzle::ErrorCode::InvalidArgument);
}
