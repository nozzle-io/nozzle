// nozzle - test_c_api_swizzle.cpp - C API swizzle regression guard

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <nozzle/nozzle_c.h>

// ---------- RGB formats: must be rejected as UNSUPPORTED_FORMAT ----------

TEST_CASE("C API: rgb8_unorm rejected (was: zero-byte copy)", "[c_api][swizzle]") {
	uint8_t buf[64]{};
	const uint8_t permute[4] = {3, 2, 1, 0};
	REQUIRE(nozzle_swizzle_channels(buf, buf, 1, 1, 16, 16,
		NOZZLE_FORMAT_RGB8_UNORM, permute) == NOZZLE_ERROR_UNSUPPORTED_FORMAT);
}

TEST_CASE("C API: rgb16_unorm rejected (was: 1-byte component size)", "[c_api][swizzle]") {
	uint8_t buf[64]{};
	const uint8_t permute[4] = {3, 2, 1, 0};
	REQUIRE(nozzle_swizzle_channels(buf, buf, 1, 1, 16, 16,
		NOZZLE_FORMAT_RGB16_UNORM, permute) == NOZZLE_ERROR_UNSUPPORTED_FORMAT);
}

TEST_CASE("C API: rgb16_float rejected (was: 1-byte component size)", "[c_api][swizzle]") {
	uint8_t buf[64]{};
	const uint8_t permute[4] = {3, 2, 1, 0};
	REQUIRE(nozzle_swizzle_channels(buf, buf, 1, 1, 16, 16,
		NOZZLE_FORMAT_RGB16_FLOAT, permute) == NOZZLE_ERROR_UNSUPPORTED_FORMAT);
}

TEST_CASE("C API: rgb32_float rejected (was: 3-byte component size)", "[c_api][swizzle]") {
	uint8_t buf[64]{};
	const uint8_t permute[4] = {3, 2, 1, 0};
	REQUIRE(nozzle_swizzle_channels(buf, buf, 1, 1, 16, 16,
		NOZZLE_FORMAT_RGB32_FLOAT, permute) == NOZZLE_ERROR_UNSUPPORTED_FORMAT);
}

TEST_CASE("C API: rgb32_uint rejected (was: 3-byte component size)", "[c_api][swizzle]") {
	uint8_t buf[64]{};
	const uint8_t permute[4] = {3, 2, 1, 0};
	REQUIRE(nozzle_swizzle_channels(buf, buf, 1, 1, 16, 16,
		NOZZLE_FORMAT_RGB32_UINT, permute) == NOZZLE_ERROR_UNSUPPORTED_FORMAT);
}

// ---------- C API error code mapping integrity ----------

TEST_CASE("C API: supported format returns OK (error code mapping guard)", "[c_api][swizzle]") {
	// Verifies that to_c_error() maps C++ ErrorCode::Ok to NOZZLE_OK
	// and does not accidentally reject supported formats.
	uint8_t src[4] = {0xAA, 0xBB, 0xCC, 0xDD};
	uint8_t dst[4]{};
	const uint8_t permute[4] = {3, 2, 1, 0};
	REQUIRE(nozzle_swizzle_channels(src, dst, 1, 1, 4, 4,
		NOZZLE_FORMAT_RGBA8_UNORM, permute) == NOZZLE_OK);
	// Verify output is not a silent no-op: at least one byte must differ
	REQUIRE(src[0] != dst[0]);
}

// ---------- Permute map validation (C API boundary) ----------

TEST_CASE("C API: permute_map value > 3 rejected before native", "[c_api][swizzle]") {
	uint8_t buf[16]{};
	const uint8_t bad_permute[4] = {0, 1, 2, 4};
	REQUIRE(nozzle_swizzle_channels(buf, buf, 1, 1, 4, 4,
		NOZZLE_FORMAT_RGBA8_UNORM, bad_permute) == NOZZLE_ERROR_INVALID_ARGUMENT);
}

TEST_CASE("C API: null permute_map rejected", "[c_api][swizzle]") {
	uint8_t buf[16]{};
	REQUIRE(nozzle_swizzle_channels(buf, buf, 1, 1, 4, 4,
		NOZZLE_FORMAT_RGBA8_UNORM, nullptr) == NOZZLE_ERROR_INVALID_ARGUMENT);
}

TEST_CASE("C API: negative src_row_bytes rejected", "[c_api][swizzle]") {
	uint8_t buf[64]{};
	const uint8_t permute[4] = {3, 2, 1, 0};
	REQUIRE(nozzle_swizzle_channels(buf, buf, 1, 1, -4, 4,
		NOZZLE_FORMAT_RGBA8_UNORM, permute) == NOZZLE_ERROR_INVALID_ARGUMENT);
}

TEST_CASE("C API: negative dst_row_bytes rejected", "[c_api][swizzle]") {
	uint8_t buf[64]{};
	const uint8_t permute[4] = {3, 2, 1, 0};
	REQUIRE(nozzle_swizzle_channels(buf, buf, 1, 1, 4, -4,
		NOZZLE_FORMAT_RGBA8_UNORM, permute) == NOZZLE_ERROR_INVALID_ARGUMENT);
}

TEST_CASE("C API: src_row_bytes > UINT32_MAX rejected", "[c_api][swizzle]") {
	uint8_t buf[64]{};
	const uint8_t permute[4] = {3, 2, 1, 0};
	int64_t huge = static_cast<int64_t>(UINT32_MAX) + 16;
	REQUIRE(nozzle_swizzle_channels(buf, buf, 1, 1, huge, 4,
		NOZZLE_FORMAT_RGBA8_UNORM, permute) == NOZZLE_ERROR_INVALID_ARGUMENT);
}

TEST_CASE("C API: dst_row_bytes > UINT32_MAX rejected", "[c_api][swizzle]") {
	uint8_t buf[64]{};
	const uint8_t permute[4] = {3, 2, 1, 0};
	int64_t huge = static_cast<int64_t>(UINT32_MAX) + 16;
	REQUIRE(nozzle_swizzle_channels(buf, buf, 1, 1, 4, huge,
		NOZZLE_FORMAT_RGBA8_UNORM, permute) == NOZZLE_ERROR_INVALID_ARGUMENT);
}
