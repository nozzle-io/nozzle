#include <catch2/catch_test_macros.hpp>

#include <nozzle/channel_swizzle.hpp>
#include <nozzle/types.hpp>

#include <cstring>
#include <vector>

using namespace nozzle;

// Helper: build a 4-channel pixel from channel values (8-bit)
static uint32_t make_pixel_8(uint8_t a, uint8_t r, uint8_t g, uint8_t b) {
	uint32_t p;
	auto *bytes = reinterpret_cast<uint8_t *>(&p);
	bytes[0] = a; bytes[1] = r; bytes[2] = g; bytes[3] = b;
	return p;
}

// Helper: build a 4-channel pixel from channel values (32-bit float)
// Memory layout: [ch0, ch1, ch2, ch3] each 4 bytes
static void make_pixel_f32(float ch0, float ch1, float ch2, float ch3,
                           uint8_t *out) {
	std::memcpy(out + 0, &ch0, 4);
	std::memcpy(out + 4, &ch1, 4);
	std::memcpy(out + 8, &ch2, 4);
	std::memcpy(out + 12, &ch3, 4);
}

static float read_channel_f32(const uint8_t *pixel, int channel) {
	float v;
	std::memcpy(&v, pixel + channel * 4, 4);
	return v;
}

// ---------- Error cases ----------

TEST_CASE("swizzle_channels: null pointers return error", "[swizzle]") {
	uint8_t buf[16]{};
	auto r = swizzle_channels(nullptr, buf, 1, 1, 4, 4,
	                          texture_format::rgba8_unorm,
	                          permute_argb_to_bgra);
	REQUIRE_FALSE(r.ok());
	REQUIRE(r.error().code == ErrorCode::InvalidArgument);

	r = swizzle_channels(buf, nullptr, 1, 1, 4, 4,
	                     texture_format::rgba8_unorm,
	                     permute_argb_to_bgra);
	REQUIRE_FALSE(r.ok());
	REQUIRE(r.error().code == ErrorCode::InvalidArgument);
}

TEST_CASE("swizzle_channels: zero dimensions return error", "[swizzle]") {
	uint8_t buf[16]{};
	auto r = swizzle_channels(buf, buf, 0, 1, 4, 4,
	                          texture_format::rgba8_unorm,
	                          permute_argb_to_bgra);
	REQUIRE_FALSE(r.ok());
	REQUIRE(r.error().code == ErrorCode::InvalidArgument);

	r = swizzle_channels(buf, buf, 1, 0, 4, 4,
	                     texture_format::rgba8_unorm,
	                     permute_argb_to_bgra);
	REQUIRE_FALSE(r.ok());
	REQUIRE(r.error().code == ErrorCode::InvalidArgument);
}

TEST_CASE("swizzle_channels: unsupported format returns error", "[swizzle]") {
	uint8_t buf[16]{};
	auto r = swizzle_channels(buf, buf, 1, 1, 4, 4,
	                          texture_format::r8_unorm,
	                          permute_argb_to_bgra);
	REQUIRE_FALSE(r.ok());
	REQUIRE(r.error().code == ErrorCode::UnsupportedFormat);
}

TEST_CASE("swizzle_channels: row_bytes too small returns error", "[swizzle]") {
	uint8_t buf[32]{};
	auto r = swizzle_channels(buf, buf, 4, 1, 8, 8,
	                          texture_format::rgba8_unorm,
	                          permute_argb_to_bgra);
	REQUIRE_FALSE(r.ok());
	REQUIRE(r.error().code == ErrorCode::InvalidArgument);

	r = swizzle_channels(buf, buf, 4, 1, 16, 8,
	                     texture_format::rgba8_unorm,
	                     permute_argb_to_bgra);
	REQUIRE_FALSE(r.ok());
	REQUIRE(r.error().code == ErrorCode::InvalidArgument);
}

// ---------- 8-bit permutation correctness ----------

TEST_CASE("swizzle_channels: ARGB→BGRA 8-bit single pixel", "[swizzle]") {
	// ARGB {A=0xAA, R=0xBB, G=0xCC, B=0xDD}
	uint32_t src = make_pixel_8(0xAA, 0xBB, 0xCC, 0xDD);
	uint32_t dst = 0;

	auto r = swizzle_channels(&src, &dst, 1, 1, 4, 4,
	                          texture_format::rgba8_unorm,
	                          permute_argb_to_bgra);
	REQUIRE(r.ok());

	auto *b = reinterpret_cast<uint8_t *>(&dst);
	REQUIRE(b[0] == 0xDD); // B
	REQUIRE(b[1] == 0xCC); // G
	REQUIRE(b[2] == 0xBB); // R
	REQUIRE(b[3] == 0xAA); // A
}

TEST_CASE("swizzle_channels: ARGB→RGBA 8-bit single pixel", "[swizzle]") {
	uint32_t src = make_pixel_8(0xAA, 0xBB, 0xCC, 0xDD);
	uint32_t dst = 0;

	auto r = swizzle_channels(&src, &dst, 1, 1, 4, 4,
	                          texture_format::rgba8_unorm,
	                          permute_argb_to_rgba);
	REQUIRE(r.ok());

	auto *b = reinterpret_cast<uint8_t *>(&dst);
	REQUIRE(b[0] == 0xBB); // R
	REQUIRE(b[1] == 0xCC); // G
	REQUIRE(b[2] == 0xDD); // B
	REQUIRE(b[3] == 0xAA); // A
}

TEST_CASE("swizzle_channels: BGRA→ARGB 8-bit single pixel", "[swizzle]") {
	// BGRA {B=0xDD, G=0xCC, R=0xBB, A=0xAA}
	uint32_t src = make_pixel_8(0xDD, 0xCC, 0xBB, 0xAA);
	uint32_t dst = 0;

	auto r = swizzle_channels(&src, &dst, 1, 1, 4, 4,
	                          texture_format::bgra8_unorm,
	                          permute_bgra_to_argb);
	REQUIRE(r.ok());

	auto *b = reinterpret_cast<uint8_t *>(&dst);
	REQUIRE(b[0] == 0xAA); // A
	REQUIRE(b[1] == 0xBB); // R
	REQUIRE(b[2] == 0xCC); // G
	REQUIRE(b[3] == 0xDD); // B
}

TEST_CASE("swizzle_channels: RGBA→ARGB 8-bit single pixel", "[swizzle]") {
	// RGBA {R=0xBB, G=0xCC, B=0xDD, A=0xAA}
	uint32_t src = make_pixel_8(0xBB, 0xCC, 0xDD, 0xAA);
	uint32_t dst = 0;

	auto r = swizzle_channels(&src, &dst, 1, 1, 4, 4,
	                          texture_format::rgba8_unorm,
	                          permute_rgba_to_argb);
	REQUIRE(r.ok());

	auto *b = reinterpret_cast<uint8_t *>(&dst);
	REQUIRE(b[0] == 0xAA); // A
	REQUIRE(b[1] == 0xBB); // R
	REQUIRE(b[2] == 0xCC); // G
	REQUIRE(b[3] == 0xDD); // B
}

// ---------- Roundtrip (self-inverse verification) ----------

TEST_CASE("swizzle_channels: ARGB↔BGRA roundtrip is identity", "[swizzle]") {
	uint32_t original = make_pixel_8(0x11, 0x22, 0x33, 0x44);
	uint32_t tmp = 0;
	uint32_t result = 0;

	REQUIRE(swizzle_channels(&original, &tmp, 1, 1, 4, 4,
	                         texture_format::rgba8_unorm,
	                         permute_argb_to_bgra).ok());
	REQUIRE(swizzle_channels(&tmp, &result, 1, 1, 4, 4,
	                         texture_format::bgra8_unorm,
	                         permute_bgra_to_argb).ok());

	REQUIRE(std::memcmp(&original, &result, 4) == 0);
}

TEST_CASE("swizzle_channels: ARGB↔RGBA roundtrip is identity", "[swizzle]") {
	uint32_t original = make_pixel_8(0x11, 0x22, 0x33, 0x44);
	uint32_t tmp = 0;
	uint32_t result = 0;

	REQUIRE(swizzle_channels(&original, &tmp, 1, 1, 4, 4,
	                         texture_format::rgba8_unorm,
	                         permute_argb_to_rgba).ok());
	REQUIRE(swizzle_channels(&tmp, &result, 1, 1, 4, 4,
	                         texture_format::rgba8_unorm,
	                         permute_rgba_to_argb).ok());

	REQUIRE(std::memcmp(&original, &result, 4) == 0);
}

// ---------- Multi-pixel with row stride ----------

TEST_CASE("swizzle_channels: 4x2 image with padded row bytes", "[swizzle]") {
	constexpr uint32_t w = 4;
	constexpr uint32_t h = 2;
	constexpr uint32_t pixel_bytes = 4;
	constexpr uint32_t row_bytes = w * pixel_bytes + 8; // 8 bytes padding per row

	std::vector<uint8_t> src(h * row_bytes, 0);
	std::vector<uint8_t> dst(h * row_bytes, 0xFF);

	// Fill src with distinct pixels
	for (uint32_t y = 0; y < h; ++y) {
		for (uint32_t x = 0; x < w; ++x) {
			auto *p = src.data() + y * row_bytes + x * pixel_bytes;
			p[0] = static_cast<uint8_t>(0xA0 | y); // A
			p[1] = static_cast<uint8_t>(0x10 * x + 0x01); // R
			p[2] = static_cast<uint8_t>(0x10 * x + 0x02); // G
			p[3] = static_cast<uint8_t>(0x10 * x + 0x03); // B
		}
	}

	auto r = swizzle_channels(src.data(), dst.data(), w, h,
	                          row_bytes, row_bytes,
	                          texture_format::rgba8_unorm,
	                          permute_argb_to_bgra);
	REQUIRE(r.ok());

	// Verify each pixel
	for (uint32_t y = 0; y < h; ++y) {
		for (uint32_t x = 0; x < w; ++x) {
			auto *s = src.data() + y * row_bytes + x * pixel_bytes;
			auto *d = dst.data() + y * row_bytes + x * pixel_bytes;
			REQUIRE(d[0] == s[3]); // dst B = src B
			REQUIRE(d[1] == s[2]); // dst G = src G
			REQUIRE(d[2] == s[1]); // dst R = src R
			REQUIRE(d[3] == s[0]); // dst A = src A
		}
	}
}

// ---------- RGBA32_FLOAT (16 bytes per pixel) ----------

TEST_CASE("swizzle_channels: ARGB→BGRA 32-bit float single pixel", "[swizzle]") {
	alignas(16) uint8_t src[16];
	alignas(16) uint8_t dst[16];

	make_pixel_f32(1.0f, 2.0f, 3.0f, 4.0f, src);

	auto r = swizzle_channels(src, dst, 1, 1, 16, 16,
	                          texture_format::rgba32_float,
	                          permute_argb_to_bgra);
	REQUIRE(r.ok());

	REQUIRE(read_channel_f32(dst, 0) == 4.0f); // B
	REQUIRE(read_channel_f32(dst, 1) == 3.0f); // G
	REQUIRE(read_channel_f32(dst, 2) == 2.0f); // R
	REQUIRE(read_channel_f32(dst, 3) == 1.0f); // A
}

TEST_CASE("swizzle_channels: RGBA32_FLOAT roundtrip is identity", "[swizzle]") {
	alignas(16) uint8_t src[16];
	alignas(16) uint8_t tmp[16];
	alignas(16) uint8_t result[16];

	make_pixel_f32(1.5f, -2.5f, 0.0f, 100.0f, src);

	REQUIRE(swizzle_channels(src, tmp, 1, 1, 16, 16,
	                         texture_format::rgba32_float,
	                         permute_argb_to_bgra).ok());
	REQUIRE(swizzle_channels(tmp, result, 1, 1, 16, 16,
	                         texture_format::rgba32_float,
	                         permute_bgra_to_argb).ok());

	REQUIRE(std::memcmp(src, result, 16) == 0);
}

// ---------- src != dst (out-of-place verification) ----------

TEST_CASE("swizzle_channels: src and dst buffers are independent", "[swizzle]") {
	uint32_t src = make_pixel_8(0xAA, 0xBB, 0xCC, 0xDD);
	uint32_t dst = 0;

	auto r = swizzle_channels(&src, &dst, 1, 1, 4, 4,
	                          texture_format::rgba8_unorm,
	                          permute_argb_to_bgra);
	REQUIRE(r.ok());

	// src must be unchanged
	auto *s = reinterpret_cast<uint8_t *>(&src);
	REQUIRE(s[0] == 0xAA);
	REQUIRE(s[1] == 0xBB);
	REQUIRE(s[2] == 0xCC);
	REQUIRE(s[3] == 0xDD);
}
