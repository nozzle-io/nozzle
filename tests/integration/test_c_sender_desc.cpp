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

TEST_CASE("nozzle_swizzle_channels: RGB formats return UNSUPPORTED_FORMAT", "[c_api][swizzle]") {
    uint8_t buf[64]{};
    const uint8_t permute[4] = {3, 2, 1, 0};
    const NozzleTextureFormat rgb_formats[] = {
        NOZZLE_FORMAT_RGB8_UNORM,
        NOZZLE_FORMAT_RGB16_UNORM,
        NOZZLE_FORMAT_RGB16_FLOAT,
        NOZZLE_FORMAT_RGB32_FLOAT,
        NOZZLE_FORMAT_RGB32_UINT,
    };
    for (auto fmt : rgb_formats) {
        INFO("format=" << fmt);
        REQUIRE(nozzle_swizzle_channels(buf, buf, 1, 1, 16, 16, fmt, permute)
                == NOZZLE_ERROR_UNSUPPORTED_FORMAT);
    }
}
