// nozzle - test_c_api_fill_alpha.cpp - C API fill_opaque_alpha_channel tests

#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <nozzle/nozzle_c.h>

// ---------- Success cases: alpha written, RGB preserved ----------

TEST_CASE("C API: fill_opaque_alpha_channel rgba8_unorm writes 0xFF", "[c_api][fill_alpha]") {
	uint8_t pixel[4] = {0x11, 0x22, 0x33, 0x00};
	REQUIRE(nozzle_fill_opaque_alpha_channel(pixel, 1, 1, 4,
		NOZZLE_FORMAT_RGBA8_UNORM) == NOZZLE_OK);
	REQUIRE(pixel[0] == 0x11);
	REQUIRE(pixel[1] == 0x22);
	REQUIRE(pixel[2] == 0x33);
	REQUIRE(pixel[3] == 0xFF);
}

TEST_CASE("C API: fill_opaque_alpha_channel bgra8_unorm writes 0xFF", "[c_api][fill_alpha]") {
	uint8_t pixel[4] = {0x33, 0x22, 0x11, 0x00};
	REQUIRE(nozzle_fill_opaque_alpha_channel(pixel, 1, 1, 4,
		NOZZLE_FORMAT_BGRA8_UNORM) == NOZZLE_OK);
	REQUIRE(pixel[0] == 0x33);
	REQUIRE(pixel[1] == 0x22);
	REQUIRE(pixel[2] == 0x11);
	REQUIRE(pixel[3] == 0xFF);
}

TEST_CASE("C API: fill_opaque_alpha_channel rgba32_float writes 1.0f", "[c_api][fill_alpha]") {
	float pixel[4] = {0.25f, 0.5f, 0.75f, 0.0f};
	REQUIRE(nozzle_fill_opaque_alpha_channel(pixel, 1, 1, 16,
		NOZZLE_FORMAT_RGBA32_FLOAT) == NOZZLE_OK);
	REQUIRE(pixel[0] == 0.25f);
	REQUIRE(pixel[1] == 0.5f);
	REQUIRE(pixel[2] == 0.75f);
	REQUIRE(pixel[3] == 1.0f);
}

// ---------- Error cases: unsupported format ----------

TEST_CASE("C API: fill_opaque_alpha_channel rejects rgb8_unorm", "[c_api][fill_alpha]") {
	uint8_t buf[4]{};
	REQUIRE(nozzle_fill_opaque_alpha_channel(buf, 1, 1, 4,
		NOZZLE_FORMAT_RGB8_UNORM) == NOZZLE_ERROR_UNSUPPORTED_FORMAT);
}

TEST_CASE("C API: fill_opaque_alpha_channel rejects r8_unorm", "[c_api][fill_alpha]") {
	uint8_t buf[4]{};
	REQUIRE(nozzle_fill_opaque_alpha_channel(buf, 1, 1, 4,
		NOZZLE_FORMAT_R8_UNORM) == NOZZLE_ERROR_UNSUPPORTED_FORMAT);
}

TEST_CASE("C API: fill_opaque_alpha_channel rejects unknown format", "[c_api][fill_alpha]") {
	uint8_t buf[4]{};
	REQUIRE(nozzle_fill_opaque_alpha_channel(buf, 1, 1, 4,
		NOZZLE_FORMAT_UNKNOWN) == NOZZLE_ERROR_UNSUPPORTED_FORMAT);
}

// ---------- Error cases: invalid arguments ----------

TEST_CASE("C API: fill_opaque_alpha_channel null data returns INVALID_ARGUMENT", "[c_api][fill_alpha]") {
	REQUIRE(nozzle_fill_opaque_alpha_channel(nullptr, 1, 1, 4,
		NOZZLE_FORMAT_RGBA8_UNORM) == NOZZLE_ERROR_INVALID_ARGUMENT);
}

TEST_CASE("C API: fill_opaque_alpha_channel zero width returns INVALID_ARGUMENT", "[c_api][fill_alpha]") {
	uint8_t buf[4]{};
	REQUIRE(nozzle_fill_opaque_alpha_channel(buf, 0, 1, 4,
		NOZZLE_FORMAT_RGBA8_UNORM) == NOZZLE_ERROR_INVALID_ARGUMENT);
}

TEST_CASE("C API: fill_opaque_alpha_channel zero stride returns INVALID_ARGUMENT", "[c_api][fill_alpha]") {
	uint8_t buf[4]{};
	REQUIRE(nozzle_fill_opaque_alpha_channel(buf, 1, 1, 0,
		NOZZLE_FORMAT_RGBA8_UNORM) == NOZZLE_ERROR_INVALID_ARGUMENT);
}
