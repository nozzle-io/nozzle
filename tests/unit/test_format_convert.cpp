#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <nozzle/format_convert.hpp>
#include <nozzle/types.hpp>

#include <cmath>
#include <cstring>
#include <vector>

using namespace nozzle;
using Catch::Matchers::WithinAbs;

// ---------- Error cases ----------

TEST_CASE("widen_uint16_to_uint32: null pointers return error", "[format_convert]") {
	uint8_t buf[16]{};
	auto r = widen_uint16_to_uint32(nullptr, buf, 1, 1, 2, 4, 1);
	REQUIRE_FALSE(r.ok());
	REQUIRE(r.error().code == ErrorCode::InvalidArgument);

	r = widen_uint16_to_uint32(buf, nullptr, 1, 1, 2, 4, 1);
	REQUIRE_FALSE(r.ok());
	REQUIRE(r.error().code == ErrorCode::InvalidArgument);
}

TEST_CASE("widen_uint16_to_uint32: zero dimensions return error", "[format_convert]") {
	uint8_t buf[16]{};
	auto r = widen_uint16_to_uint32(buf, buf, 0, 1, 2, 4, 1);
	REQUIRE_FALSE(r.ok());
	REQUIRE(r.error().code == ErrorCode::InvalidArgument);

	r = widen_uint16_to_uint32(buf, buf, 1, 0, 2, 4, 1);
	REQUIRE_FALSE(r.ok());
	REQUIRE(r.error().code == ErrorCode::InvalidArgument);
}

TEST_CASE("widen_uint16_to_uint32: row_bytes too small returns error", "[format_convert]") {
	uint8_t buf[32]{};
	auto r = widen_uint16_to_uint32(buf, buf, 4, 1, 4, 16, 1);
	REQUIRE_FALSE(r.ok());
	REQUIRE(r.error().code == ErrorCode::InvalidArgument);

	r = widen_uint16_to_uint32(buf, buf, 4, 1, 8, 8, 1);
	REQUIRE_FALSE(r.ok());
	REQUIRE(r.error().code == ErrorCode::InvalidArgument);
}

TEST_CASE("convert_uint32_to_float32: null pointers return error", "[format_convert]") {
	uint8_t buf[16]{};
	auto r = convert_uint32_to_float32(nullptr, buf, 1, 1, 4, 4, 1);
	REQUIRE_FALSE(r.ok());
	REQUIRE(r.error().code == ErrorCode::InvalidArgument);

	r = convert_uint32_to_float32(buf, nullptr, 1, 1, 4, 4, 1);
	REQUIRE_FALSE(r.ok());
	REQUIRE(r.error().code == ErrorCode::InvalidArgument);
}

TEST_CASE("convert_uint32_to_float32: zero dimensions return error", "[format_convert]") {
	uint8_t buf[16]{};
	auto r = convert_uint32_to_float32(buf, buf, 0, 1, 4, 4, 1);
	REQUIRE_FALSE(r.ok());
	REQUIRE(r.error().code == ErrorCode::InvalidArgument);

	r = convert_uint32_to_float32(buf, buf, 1, 0, 4, 4, 1);
	REQUIRE_FALSE(r.ok());
	REQUIRE(r.error().code == ErrorCode::InvalidArgument);
}

TEST_CASE("convert_uint32_to_float32: row_bytes too small returns error", "[format_convert]") {
	uint8_t buf[32]{};
	auto r = convert_uint32_to_float32(buf, buf, 4, 1, 8, 16, 1);
	REQUIRE_FALSE(r.ok());
	REQUIRE(r.error().code == ErrorCode::InvalidArgument);

	r = convert_uint32_to_float32(buf, buf, 4, 1, 16, 8, 1);
	REQUIRE_FALSE(r.ok());
	REQUIRE(r.error().code == ErrorCode::InvalidArgument);
}

// ---------- widen_uint16_to_uint32 correctness ----------

TEST_CASE("widen_uint16_to_uint32: single element", "[format_convert]") {
	uint16_t src = 0xABCD;
	uint32_t dst = 0;
	auto r = widen_uint16_to_uint32(&src, &dst, 1, 1, 2, 4, 1);
	REQUIRE(r.ok());
	REQUIRE(dst == 0x0000ABCD);
}

TEST_CASE("widen_uint16_to_uint32: zero value", "[format_convert]") {
	uint16_t src = 0;
	uint32_t dst = 0xFFFFFFFF;
	auto r = widen_uint16_to_uint32(&src, &dst, 1, 1, 2, 4, 1);
	REQUIRE(r.ok());
	REQUIRE(dst == 0);
}

TEST_CASE("widen_uint16_to_uint32: max uint16 value", "[format_convert]") {
	uint16_t src = 0xFFFF;
	uint32_t dst = 0;
	auto r = widen_uint16_to_uint32(&src, &dst, 1, 1, 2, 4, 1);
	REQUIRE(r.ok());
	REQUIRE(dst == 0x0000FFFF);
}

TEST_CASE("widen_uint16_to_uint32: 4-channel single pixel (RGBA16)", "[format_convert]") {
	uint16_t src[4] = {0x1234, 0x5678, 0x9ABC, 0xDEF0};
	uint32_t dst[4] = {};
	auto r = widen_uint16_to_uint32(src, dst, 1, 1, 8, 16, 4);
	REQUIRE(r.ok());
	REQUIRE(dst[0] == 0x00001234);
	REQUIRE(dst[1] == 0x00005678);
	REQUIRE(dst[2] == 0x00009ABC);
	REQUIRE(dst[3] == 0x0000DEF0);
}

TEST_CASE("widen_uint16_to_uint32: multi-row with padded stride", "[format_convert]") {
	constexpr uint32_t w = 3;
	constexpr uint32_t h = 2;
	constexpr uint32_t channels = 4;
	constexpr uint32_t src_row = w * channels * 2 + 8;
	constexpr uint32_t dst_row = w * channels * 4 + 16;

	std::vector<uint8_t> src(h * src_row, 0);
	std::vector<uint8_t> dst(h * dst_row, 0xFF);

	for (uint32_t y = 0; y < h; ++y) {
		auto *row = reinterpret_cast<uint16_t *>(src.data() + y * src_row);
		for (uint32_t i = 0; i < w * channels; ++i) {
			row[i] = static_cast<uint16_t>(y * 1000 + i);
		}
	}

	auto r = widen_uint16_to_uint32(src.data(), dst.data(), w, h, src_row, dst_row, channels);
	REQUIRE(r.ok());

	for (uint32_t y = 0; y < h; ++y) {
		auto *src_row_ptr = reinterpret_cast<const uint16_t *>(src.data() + y * src_row);
		auto *dst_row_ptr = reinterpret_cast<const uint32_t *>(dst.data() + y * dst_row);
		for (uint32_t i = 0; i < w * channels; ++i) {
			REQUIRE(dst_row_ptr[i] == static_cast<uint32_t>(src_row_ptr[i]));
		}
	}
}

TEST_CASE("widen_uint16_to_uint32: SIMD boundary sizes", "[format_convert]") {
	std::vector<uint16_t> src(32, 0);
	std::vector<uint32_t> dst(32, 0);

	for (uint16_t i = 0; i < 32; ++i) src[i] = i;

	SECTION("1 element (below SIMD stride)") {
		std::fill(dst.begin(), dst.end(), 0xDEAD);
		auto r = widen_uint16_to_uint32(src.data(), dst.data(), 1, 1, 2, 4, 1);
		REQUIRE(r.ok());
		REQUIRE(dst[0] == 0);
	}

	SECTION("4 elements (one NEON/SSE2 vector)") {
		std::fill(dst.begin(), dst.end(), 0xDEAD);
		auto r = widen_uint16_to_uint32(src.data(), dst.data(), 4, 1, 8, 16, 1);
		REQUIRE(r.ok());
		for (uint32_t i = 0; i < 4; ++i) REQUIRE(dst[i] == i);
	}

	SECTION("5 elements (vector + scalar tail)") {
		std::fill(dst.begin(), dst.end(), 0xDEAD);
		auto r = widen_uint16_to_uint32(src.data(), dst.data(), 5, 1, 10, 20, 1);
		REQUIRE(r.ok());
		for (uint32_t i = 0; i < 5; ++i) REQUIRE(dst[i] == i);
	}

	SECTION("8 elements (two SSE2 vectors)") {
		std::fill(dst.begin(), dst.end(), 0xDEAD);
		auto r = widen_uint16_to_uint32(src.data(), dst.data(), 8, 1, 16, 32, 1);
		REQUIRE(r.ok());
		for (uint32_t i = 0; i < 8; ++i) REQUIRE(dst[i] == i);
	}

	SECTION("9 elements (two SSE2 vectors + scalar tail)") {
		std::fill(dst.begin(), dst.end(), 0xDEAD);
		auto r = widen_uint16_to_uint32(src.data(), dst.data(), 9, 1, 18, 36, 1);
		REQUIRE(r.ok());
		for (uint32_t i = 0; i < 9; ++i) REQUIRE(dst[i] == i);
	}
}

// ---------- convert_uint32_to_float32 correctness ----------

TEST_CASE("convert_uint32_to_float32: single element", "[format_convert]") {
	uint32_t src = 42;
	float dst = -1.0f;
	auto r = convert_uint32_to_float32(&src, &dst, 1, 1, 4, 4, 1);
	REQUIRE(r.ok());
	REQUIRE(dst == 42.0f);
}

TEST_CASE("convert_uint32_to_float32: zero value", "[format_convert]") {
	uint32_t src = 0;
	float dst = -1.0f;
	auto r = convert_uint32_to_float32(&src, &dst, 1, 1, 4, 4, 1);
	REQUIRE(r.ok());
	REQUIRE(dst == 0.0f);
}

TEST_CASE("convert_uint32_to_float32: large value", "[format_convert]") {
	uint32_t src = 1000000;
	float dst = -1.0f;
	auto r = convert_uint32_to_float32(&src, &dst, 1, 1, 4, 4, 1);
	REQUIRE(r.ok());
	REQUIRE_THAT(dst, WithinAbs(1000000.0f, 0.5f));
}

TEST_CASE("convert_uint32_to_float32: 4-channel single pixel", "[format_convert]") {
	uint32_t src[4] = {100, 200, 300, 400};
	float dst[4] = {};
	auto r = convert_uint32_to_float32(src, dst, 1, 1, 16, 16, 4);
	REQUIRE(r.ok());
	REQUIRE(dst[0] == 100.0f);
	REQUIRE(dst[1] == 200.0f);
	REQUIRE(dst[2] == 300.0f);
	REQUIRE(dst[3] == 400.0f);
}

TEST_CASE("convert_uint32_to_float32: multi-row with padded stride", "[format_convert]") {
	constexpr uint32_t w = 3;
	constexpr uint32_t h = 2;
	constexpr uint32_t channels = 4;
	constexpr uint32_t src_row = w * channels * 4 + 16;
	constexpr uint32_t dst_row = w * channels * 4 + 16;

	std::vector<uint8_t> src(h * src_row, 0);
	std::vector<uint8_t> dst(h * dst_row, 0);

	for (uint32_t y = 0; y < h; ++y) {
		auto *row = reinterpret_cast<uint32_t *>(src.data() + y * src_row);
		for (uint32_t i = 0; i < w * channels; ++i) {
			row[i] = y * 1000 + i;
		}
	}

	auto r = convert_uint32_to_float32(src.data(), dst.data(), w, h, src_row, dst_row, channels);
	REQUIRE(r.ok());

	for (uint32_t y = 0; y < h; ++y) {
		auto *src_row_ptr = reinterpret_cast<const uint32_t *>(src.data() + y * src_row);
		auto *dst_row_ptr = reinterpret_cast<const float *>(dst.data() + y * dst_row);
		for (uint32_t i = 0; i < w * channels; ++i) {
			REQUIRE_THAT(dst_row_ptr[i], WithinAbs(static_cast<float>(src_row_ptr[i]), 0.001f));
		}
	}
}

TEST_CASE("convert_uint32_to_float32: SIMD boundary sizes", "[format_convert]") {
	std::vector<uint32_t> src(16, 0);
	std::vector<float> dst(16, -1.0f);

	for (uint32_t i = 0; i < 16; ++i) src[i] = i * 100;

	SECTION("1 element (below SIMD stride)") {
		auto r = convert_uint32_to_float32(src.data(), dst.data(), 1, 1, 4, 4, 1);
		REQUIRE(r.ok());
		REQUIRE(dst[0] == 0.0f);
	}

	SECTION("4 elements (one vector)") {
		auto r = convert_uint32_to_float32(src.data(), dst.data(), 4, 1, 16, 16, 1);
		REQUIRE(r.ok());
		for (uint32_t i = 0; i < 4; ++i) REQUIRE(dst[i] == static_cast<float>(i * 100));
	}

	SECTION("5 elements (vector + scalar tail)") {
		auto r = convert_uint32_to_float32(src.data(), dst.data(), 5, 1, 20, 20, 1);
		REQUIRE(r.ok());
		for (uint32_t i = 0; i < 5; ++i) REQUIRE(dst[i] == static_cast<float>(i * 100));
	}

	SECTION("8 elements (two vectors)") {
		auto r = convert_uint32_to_float32(src.data(), dst.data(), 8, 1, 32, 32, 1);
		REQUIRE(r.ok());
		for (uint32_t i = 0; i < 8; ++i) REQUIRE(dst[i] == static_cast<float>(i * 100));
	}
}

// ---------- widen_half_to_float correctness ----------

TEST_CASE("widen_half_to_float: zero", "[format_convert]") {
	uint16_t src = 0x0000;
	float dst = -1.0f;
	auto r = widen_half_to_float(&src, &dst, 1, 1, 2, 4, 1);
	REQUIRE(r.ok());
	REQUIRE(dst == 0.0f);
}

TEST_CASE("widen_half_to_float: positive 1.0", "[format_convert]") {
	uint16_t src = 0x3C00; // half-float 1.0
	float dst = -1.0f;
	auto r = widen_half_to_float(&src, &dst, 1, 1, 2, 4, 1);
	REQUIRE(r.ok());
	REQUIRE(dst == 1.0f);
}

TEST_CASE("widen_half_to_float: negative 1.0", "[format_convert]") {
	uint16_t src = 0xBC00; // half-float -1.0
	float dst = -1.0f;
	auto r = widen_half_to_float(&src, &dst, 1, 1, 2, 4, 1);
	REQUIRE(r.ok());
	REQUIRE(dst == -1.0f);
}

TEST_CASE("widen_half_to_float: 0.5", "[format_convert]") {
	uint16_t src = 0x3800; // half-float 0.5
	float dst = -1.0f;
	auto r = widen_half_to_float(&src, &dst, 1, 1, 2, 4, 1);
	REQUIRE(r.ok());
	REQUIRE(dst == 0.5f);
}

TEST_CASE("widen_half_to_float: smallest positive subnormal", "[format_convert]") {
	uint16_t src = 0x0001;
	float dst = -1.0f;
	auto r = widen_half_to_float(&src, &dst, 1, 1, 2, 4, 1);
	REQUIRE(r.ok());
	REQUIRE(dst > 0.0f);
	REQUIRE(dst < 0.001f);
}

TEST_CASE("widen_half_to_float: infinity", "[format_convert]") {
	uint16_t src = 0x7C00;
	float dst = -1.0f;
	auto r = widen_half_to_float(&src, &dst, 1, 1, 2, 4, 1);
	REQUIRE(r.ok());
	REQUIRE(std::isinf(dst));
	REQUIRE(dst > 0);
}

TEST_CASE("widen_half_to_float: negative infinity", "[format_convert]") {
	uint16_t src = 0xFC00;
	float dst = -1.0f;
	auto r = widen_half_to_float(&src, &dst, 1, 1, 2, 4, 1);
	REQUIRE(r.ok());
	REQUIRE(std::isinf(dst));
	REQUIRE(dst < 0);
}

TEST_CASE("widen_half_to_float: NaN", "[format_convert]") {
	uint16_t src = 0x7E00;
	float dst = -1.0f;
	auto r = widen_half_to_float(&src, &dst, 1, 1, 2, 4, 1);
	REQUIRE(r.ok());
	REQUIRE(std::isnan(dst));
}

TEST_CASE("widen_half_to_float: 4-channel pixel", "[format_convert]") {
	uint16_t src[4] = {0x3C00, 0x4000, 0x4200, 0x4400}; // 1.0, 2.0, 3.0, 4.0
	float dst[4] = {};
	auto r = widen_half_to_float(src, dst, 1, 1, 8, 16, 4);
	REQUIRE(r.ok());
	REQUIRE(dst[0] == 1.0f);
	REQUIRE(dst[1] == 2.0f);
	REQUIRE(dst[2] == 3.0f);
	REQUIRE(dst[3] == 4.0f);
}

TEST_CASE("widen_half_to_float: multi-row with stride", "[format_convert]") {
	constexpr uint32_t w = 2;
	constexpr uint32_t h = 3;
	constexpr uint32_t channels = 2;
	constexpr uint32_t src_row = w * channels * 2 + 4;
	constexpr uint32_t dst_row = w * channels * 4 + 8;

	std::vector<uint8_t> src(h * src_row, 0);
	std::vector<uint8_t> dst(h * dst_row, 0);

	auto *s = reinterpret_cast<uint16_t *>(src.data());
	s[0] = 0x3C00; s[1] = 0x4000;

	auto r = widen_half_to_float(src.data(), dst.data(), w, h, src_row, dst_row, channels);
	REQUIRE(r.ok());

	auto *d = reinterpret_cast<const float *>(dst.data());
	REQUIRE(d[0] == 1.0f);
	REQUIRE(d[1] == 2.0f);
}

TEST_CASE("widen_half_to_float: error on null", "[format_convert]") {
	uint8_t buf[16]{};
	auto r = widen_half_to_float(nullptr, buf, 1, 1, 2, 4, 1);
	REQUIRE_FALSE(r.ok());
	REQUIRE(r.error().code == ErrorCode::InvalidArgument);
}

TEST_CASE("widen_half_to_float: error on zero dimensions", "[format_convert]") {
	uint8_t buf[16]{};
	auto r = widen_half_to_float(buf, buf, 0, 1, 2, 4, 1);
	REQUIRE_FALSE(r.ok());
	REQUIRE(r.error().code == ErrorCode::InvalidArgument);
}

// ---------- safe_mul_u32 overflow guard (P2-T1) ----------

TEST_CASE("widen_uint16_to_uint32: overflow on width*channels rejected", "[format_convert]") {
	uint8_t buf[16]{};
	auto r = widen_uint16_to_uint32(buf, buf, UINT32_MAX, 2, 4, 8, 2);
	REQUIRE_FALSE(r.ok());
	REQUIRE(r.error().code == ErrorCode::InvalidArgument);
}

TEST_CASE("convert_uint32_to_float32: overflow on width*channels rejected", "[format_convert]") {
	uint8_t buf[16]{};
	auto r = convert_uint32_to_float32(buf, buf, UINT32_MAX / 2 + 1, 1, 4, 4, 2);
	REQUIRE_FALSE(r.ok());
	REQUIRE(r.error().code == ErrorCode::InvalidArgument);
}

TEST_CASE("widen_half_to_float: overflow on width*channels rejected", "[format_convert]") {
	uint8_t buf[16]{};
	auto r = widen_half_to_float(buf, buf, UINT32_MAX, 1, 2, 4, 2);
	REQUIRE_FALSE(r.ok());
	REQUIRE(r.error().code == ErrorCode::InvalidArgument);
}

// ---------- half-float additional edge cases (P2-T7) ----------

TEST_CASE("widen_half_to_float: largest subnormal", "[format_convert]") {
	uint16_t src = 0x03FF;
	float dst = -1.0f;
	auto r = widen_half_to_float(&src, &dst, 1, 1, 2, 4, 1);
	REQUIRE(r.ok());
	REQUIRE(dst > 0.0f);
	REQUIRE(dst < 0.001f);
}

TEST_CASE("widen_half_to_float: smallest normal", "[format_convert]") {
	uint16_t src = 0x0400;
	float dst = -1.0f;
	auto r = widen_half_to_float(&src, &dst, 1, 1, 2, 4, 1);
	REQUIRE(r.ok());
	REQUIRE_THAT(dst, WithinAbs(6.1035e-5f, 1e-8f));
}

TEST_CASE("widen_half_to_float: largest finite half", "[format_convert]") {
	uint16_t src = 0x7BFF;
	float dst = -1.0f;
	auto r = widen_half_to_float(&src, &dst, 1, 1, 2, 4, 1);
	REQUIRE(r.ok());
	REQUIRE(dst > 65000.0f);
	REQUIRE(std::isfinite(dst));
}

TEST_CASE("widen_half_to_float: negative zero", "[format_convert]") {
	uint16_t src = 0x8000;
	float dst = 1.0f;
	auto r = widen_half_to_float(&src, &dst, 1, 1, 2, 4, 1);
	REQUIRE(r.ok());
	REQUIRE(dst == 0.0f);
	REQUIRE(std::signbit(dst));
}

TEST_CASE("widen_half_to_float: quiet NaN with payload", "[format_convert]") {
	uint16_t src = 0x7FFF;
	float dst = -1.0f;
	auto r = widen_half_to_float(&src, &dst, 1, 1, 2, 4, 1);
	REQUIRE(r.ok());
	REQUIRE(std::isnan(dst));
}
