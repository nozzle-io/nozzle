#include <catch2/catch_test_macros.hpp>

#include <nozzle/nozzle_c.h>

TEST_CASE("resolve_fallback_flags: unknown bits rejected", "[c_api]") {
    NozzleSenderDesc desc{};
    desc.fallback_flags_valid = 1;
    desc.fallback_flags = 0x80000000u;

    uint32_t flags{0xFFFFFFFFu};
    REQUIRE(nozzle_resolve_fallback_flags(&desc, &flags) == NOZZLE_ERROR_INVALID_ARGUMENT);
    REQUIRE(flags == 0xFFFFFFFFu);
}

TEST_CASE("resolve_fallback_flags: unknown bits with valid bits rejected", "[c_api]") {
    NozzleSenderDesc desc{};
    desc.fallback_flags_valid = 1;
    desc.fallback_flags = 0x80000000u | NOZZLE_FALLBACK_STORAGE_COMPATIBLE;

    uint32_t flags{0};
    REQUIRE(nozzle_resolve_fallback_flags(&desc, &flags) == NOZZLE_ERROR_INVALID_ARGUMENT);
}

TEST_CASE("resolve_fallback_flags: new API flags=SAFE_DEFAULTS", "[c_api]") {
    NozzleSenderDesc desc{};
    desc.fallback_flags_valid = 1;
    desc.fallback_flags = NOZZLE_FALLBACK_SAFE_DEFAULTS;

    uint32_t flags{0};
    REQUIRE(nozzle_resolve_fallback_flags(&desc, &flags) == NOZZLE_OK);
    REQUIRE(flags == NOZZLE_FALLBACK_SAFE_DEFAULTS);
}

TEST_CASE("resolve_fallback_flags: new API flags=NONE ignores allow_format_fallback", "[c_api]") {
    NozzleSenderDesc desc{};
    desc.fallback_flags_valid = 1;
    desc.fallback_flags = NOZZLE_FALLBACK_NONE;
    desc.allow_format_fallback = 1;

    uint32_t flags{0xFF};
    REQUIRE(nozzle_resolve_fallback_flags(&desc, &flags) == NOZZLE_OK);
    REQUIRE(flags == NOZZLE_FALLBACK_NONE);
}

TEST_CASE("resolve_fallback_flags: new API flags=QUALITY_LOSS only", "[c_api]") {
    NozzleSenderDesc desc{};
    desc.fallback_flags_valid = 1;
    desc.fallback_flags = NOZZLE_FALLBACK_QUALITY_LOSS;

    uint32_t flags{0};
    REQUIRE(nozzle_resolve_fallback_flags(&desc, &flags) == NOZZLE_OK);
    REQUIRE(flags == NOZZLE_FALLBACK_QUALITY_LOSS);
}

TEST_CASE("resolve_fallback_flags: new API flags=STORAGE|QUALITY_LOSS", "[c_api]") {
    NozzleSenderDesc desc{};
    desc.fallback_flags_valid = 1;
    desc.fallback_flags = NOZZLE_FALLBACK_STORAGE_COMPATIBLE | NOZZLE_FALLBACK_QUALITY_LOSS;

    uint32_t flags{0};
    REQUIRE(nozzle_resolve_fallback_flags(&desc, &flags) == NOZZLE_OK);
    REQUIRE(flags == (NOZZLE_FALLBACK_STORAGE_COMPATIBLE | NOZZLE_FALLBACK_QUALITY_LOSS));
}

TEST_CASE("resolve_fallback_flags: legacy allow=1, flags_valid=0 -> legacy all", "[c_api]") {
    NozzleSenderDesc desc{};
    desc.allow_format_fallback = 1;
    desc.fallback_flags_valid = 0;

    uint32_t flags{0};
    REQUIRE(nozzle_resolve_fallback_flags(&desc, &flags) == NOZZLE_OK);
    REQUIRE(flags == (NOZZLE_FALLBACK_SAFE_DEFAULTS | NOZZLE_FALLBACK_QUALITY_LOSS));
}

TEST_CASE("resolve_fallback_flags: legacy allow=0, flags_valid=0 -> no fallback", "[c_api]") {
    NozzleSenderDesc desc{};
    desc.allow_format_fallback = 0;
    desc.fallback_flags_valid = 0;

    uint32_t flags{0xFF};
    REQUIRE(nozzle_resolve_fallback_flags(&desc, &flags) == NOZZLE_OK);
    REQUIRE(flags == NOZZLE_FALLBACK_NONE);
}

TEST_CASE("resolve_fallback_flags: {0} init -> no fallback", "[c_api]") {
    NozzleSenderDesc desc{};

    uint32_t flags{0xFF};
    REQUIRE(nozzle_resolve_fallback_flags(&desc, &flags) == NOZZLE_OK);
    REQUIRE(flags == NOZZLE_FALLBACK_NONE);
}

TEST_CASE("resolve_fallback_flags: null desc -> INVALID_ARGUMENT", "[c_api]") {
    uint32_t flags{0};
    REQUIRE(nozzle_resolve_fallback_flags(nullptr, &flags) == NOZZLE_ERROR_INVALID_ARGUMENT);
}

TEST_CASE("resolve_fallback_flags: null out_flags -> INVALID_ARGUMENT", "[c_api]") {
    NozzleSenderDesc desc{};
    REQUIRE(nozzle_resolve_fallback_flags(&desc, nullptr) == NOZZLE_ERROR_INVALID_ARGUMENT);
}

TEST_CASE("C API: nozzle_sender_get_native_device null checks", "[c_api][native_device]") {
    NozzleNativeDevice nd{};
    REQUIRE(nozzle_sender_get_native_device(nullptr, &nd) == NOZZLE_ERROR_INVALID_ARGUMENT);
}

TEST_CASE("C API: nozzle_sender_get_native_device returns default device", "[c_api][native_device]") {
    NozzleSenderDesc desc{};
    desc.name = "test_c_native_default";

    NozzleSender *sender = nullptr;
    NozzleErrorCode ec = nozzle_sender_create(&desc, &sender);
    if (ec != NOZZLE_OK) { return; }

    NozzleNativeDevice nd{};
    ec = nozzle_sender_get_native_device(sender, &nd);
    REQUIRE(ec == NOZZLE_OK);
    REQUIRE(nd.device != nullptr);
    REQUIRE(nd.backend == NOZZLE_BACKEND_METAL);

    nozzle_sender_destroy(sender);
}
