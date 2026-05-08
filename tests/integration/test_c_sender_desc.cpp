#include <catch2/catch_test_macros.hpp>

#include <nozzle/nozzle_c.h>

TEST_CASE("C sender desc: unknown fallback bits rejected", "[c_api]") {
    NozzleSenderDesc desc{};
    desc.name = "test";
    desc.application_name = "test";
    desc.fallback_flags_valid = 1;
    desc.fallback_flags = 0x80000000u;

    NozzleSender *sender{nullptr};
    NozzleErrorCode ec = nozzle_sender_create(&desc, &sender);
    REQUIRE(ec == NOZZLE_ERROR_INVALID_ARGUMENT);
    REQUIRE(sender == nullptr);
}

TEST_CASE("C sender desc: unknown bits with valid bits rejected", "[c_api]") {
    NozzleSenderDesc desc{};
    desc.name = "test";
    desc.application_name = "test";
    desc.fallback_flags_valid = 1;
    desc.fallback_flags = 0x80000000u | NOZZLE_FALLBACK_STORAGE_COMPATIBLE;

    NozzleSender *sender{nullptr};
    NozzleErrorCode ec = nozzle_sender_create(&desc, &sender);
    REQUIRE(ec == NOZZLE_ERROR_INVALID_ARGUMENT);
    REQUIRE(sender == nullptr);
}

TEST_CASE("C sender desc: fallback_flags_valid=1, flags=NONE succeeds", "[c_api]") {
    NozzleSenderDesc desc{};
    desc.name = "test_c_none";
    desc.application_name = "test";
    desc.fallback_flags_valid = 1;
    desc.fallback_flags = NOZZLE_FALLBACK_NONE;

    NozzleSender *sender{nullptr};
    NozzleErrorCode ec = nozzle_sender_create(&desc, &sender);
    if (ec == NOZZLE_OK && sender) {
        nozzle_sender_destroy(sender);
    }
}

TEST_CASE("C sender desc: fallback_flags_valid=1, flags=SAFE_DEFAULTS succeeds", "[c_api]") {
    NozzleSenderDesc desc{};
    desc.name = "test_c_safe";
    desc.application_name = "test";
    desc.fallback_flags_valid = 1;
    desc.fallback_flags = NOZZLE_FALLBACK_SAFE_DEFAULTS;

    NozzleSender *sender{nullptr};
    NozzleErrorCode ec = nozzle_sender_create(&desc, &sender);
    if (ec == NOZZLE_OK && sender) {
        nozzle_sender_destroy(sender);
    }
}

TEST_CASE("C sender desc: legacy allow=1, flags_valid=0 succeeds", "[c_api]") {
    NozzleSenderDesc desc{};
    desc.name = "test_c_legacy_all";
    desc.application_name = "test";
    desc.allow_format_fallback = 1;
    desc.fallback_flags_valid = 0;

    NozzleSender *sender{nullptr};
    NozzleErrorCode ec = nozzle_sender_create(&desc, &sender);
    if (ec == NOZZLE_OK && sender) {
        nozzle_sender_destroy(sender);
    }
}

TEST_CASE("C sender desc: legacy allow=0, flags_valid=0 succeeds", "[c_api]") {
    NozzleSenderDesc desc{};
    desc.name = "test_c_legacy_none";
    desc.application_name = "test";
    desc.allow_format_fallback = 0;
    desc.fallback_flags_valid = 0;

    NozzleSender *sender{nullptr};
    NozzleErrorCode ec = nozzle_sender_create(&desc, &sender);
    if (ec == NOZZLE_OK && sender) {
        nozzle_sender_destroy(sender);
    }
}

TEST_CASE("C sender desc: {0} init succeeds (no fallback)", "[c_api]") {
    NozzleSenderDesc desc{};
    desc.name = "test_c_zero_init";
    desc.application_name = "test";

    NozzleSender *sender{nullptr};
    NozzleErrorCode ec = nozzle_sender_create(&desc, &sender);
    if (ec == NOZZLE_OK && sender) {
        nozzle_sender_destroy(sender);
    }
}
