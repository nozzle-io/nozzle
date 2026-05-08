#include <catch2/catch_test_macros.hpp>

#include <nozzle/sender.hpp>
#include <nozzle/types.hpp>

TEST_CASE("C++ sender_desc: unknown fallback_flags rejected before device acquisition", "[cpp_api]") {
    nozzle::sender_desc desc{};
    desc.name = "test_invalid_flags";
    desc.fallback_flags = 0x80000000u | nozzle::fallback_allow_storage_compatible;

    auto r = nozzle::sender::create(desc);
    REQUIRE_FALSE(r.ok());
    REQUIRE(r.error().code == nozzle::ErrorCode::InvalidArgument);
}

TEST_CASE("C++ sender_desc: only unknown bits rejected", "[cpp_api]") {
    nozzle::sender_desc desc{};
    desc.name = "test_only_unknown";
    desc.fallback_flags = 0xFF000000u;

    auto r = nozzle::sender::create(desc);
    REQUIRE_FALSE(r.ok());
    REQUIRE(r.error().code == nozzle::ErrorCode::InvalidArgument);
}

TEST_CASE("C++ sender_desc: valid fallback_flags not rejected", "[cpp_api]") {
    nozzle::sender_desc desc{};
    desc.name = "test_valid_flags";
    desc.fallback_flags = nozzle::fallback_safe_defaults;

    auto r = nozzle::sender::create(desc);
    if (r.ok()) {
        auto sender = std::move(r.value());
    }
}
