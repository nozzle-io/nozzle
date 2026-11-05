// nozzle - test_simd_parity.cpp - accelerated conversion paths vs independent references
//
// The public conversion entry points choose SSE2, F16C, NEON, vImage, or
// scalar code at compile time. Short hand-written cases cannot catch lane
// bugs such as an unwritten upper half of a SIMD block or a signed conversion
// of unsigned lanes. These tests compare every output element of whichever
// path was compiled against a reference written independently of the library.
// They cover:
//   - every row length from 1 to 40 elements, so each 4- and 8-wide block and
//     each tail length occurs;
//   - multiple rows with padded strides, and buffers that are not 16-byte
//     aligned;
//   - value sets that put edge values inside SIMD blocks, not only in tails.
// They also verify that source bytes and destination padding are untouched.
// Source and destination padding use different sentinels, so a kernel that
// copies padding bytes past the row end is detected.
//
// Limits: writes are checked only within the allocation, which ends 16 bytes
// after the last row; a stray write further away needs ASan. Reads past the
// source row end cannot be detected (there are no guard pages). On Linux and Windows, swizzle_channels has only a scalar path, so its
// parity case adds SIMD coverage only where vImage is used (macOS). On macOS,
// vImage also takes the half-float path before F16C or NEON FP16 could run;
// those paths are covered by the dedicated CI lanes below.
//
// CI lanes that must exercise a specific path define NOZZLE_TEST_EXPECT_F16C
// or NOZZLE_TEST_EXPECT_NEON_FP16. Compilation then fails if that path was not
// compiled in, so a lane cannot silently fall back to scalar code.

#include <catch2/catch_test_macros.hpp>

#include <nozzle/channel_swizzle.hpp>
#include <nozzle/format_convert.hpp>
#include <nozzle/types.hpp>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

#if defined(NOZZLE_TEST_EXPECT_F16C) && !defined(__F16C__)
#error "NOZZLE_TEST_EXPECT_F16C is set, but this build does not enable F16C (__F16C__ is undefined)"
#endif

#if defined(NOZZLE_TEST_EXPECT_NEON_FP16) && !(defined(__ARM_NEON) && defined(__ARM_FP16_FORMAT_IEEE))
#error "NOZZLE_TEST_EXPECT_NEON_FP16 is set, but this build does not enable the NEON FP16 conversion path"
#endif

using namespace nozzle;

namespace {

constexpr uint8_t padding_sentinel = 0xA5;     // destination padding
constexpr uint8_t src_padding_sentinel = 0x5A; // source padding, distinct on purpose
constexpr uint32_t parity_height = 3;
constexpr uint32_t max_row_elements = 40;

uint32_t mix32(uint32_t x) {
	x += 0x9E3779B9u;
	x = (x ^ (x >> 16)) * 0x85EBCA6Bu;
	x = (x ^ (x >> 13)) * 0xC2B2AE35u;
	return x ^ (x >> 16);
}

struct plane_layout {
	uint32_t width;
	uint32_t height;
	uint32_t channels;
	uint32_t element_bytes;
	uint32_t offset_bytes;
	uint32_t pad_bytes;

	uint32_t row_payload_bytes() const { return width * channels * element_bytes; }
	uint32_t row_bytes() const { return row_payload_bytes() + pad_bytes; }
	std::size_t element_offset(uint32_t y, uint32_t i) const {
		return offset_bytes + static_cast<std::size_t>(y) * row_bytes()
			+ static_cast<std::size_t>(i) * element_bytes;
	}
	// The allocation keeps 16 sentinel bytes after the last row, so an over-long
	// store past the final row is detected as padding damage.
	std::size_t allocation_bytes() const {
		return offset_bytes + static_cast<std::size_t>(row_bytes()) * height + 16;
	}
};

std::string describe(const plane_layout &src, const plane_layout &dst) {
	std::ostringstream s;
	s << "width=" << src.width << " height=" << src.height << " channels=" << src.channels
	  << " src_row_bytes=" << src.row_bytes() << " dst_row_bytes=" << dst.row_bytes()
	  << " src_offset=" << src.offset_bytes << " dst_offset=" << dst.offset_bytes;
	return s.str();
}

std::string find_padding_damage(const std::vector<uint8_t> &buffer, const plane_layout &layout) {
	for (std::size_t b = 0; b < buffer.size(); ++b) {
		bool in_payload = false;
		if (b >= layout.offset_bytes) {
			const std::size_t relative = b - layout.offset_bytes;
			const std::size_t row = relative / layout.row_bytes();
			const std::size_t column = relative % layout.row_bytes();
			in_payload = row < layout.height && column < layout.row_payload_bytes();
		}
		if (!in_payload && buffer[b] != padding_sentinel) {
			std::ostringstream s;
			s << "destination byte " << b << " outside the row payload changed to 0x"
			  << std::hex << static_cast<int>(buffer[b]);
			return s.str();
		}
	}
	return {};
}

template <typename T>
uint32_t bits_of(T value) {
	static_assert(sizeof(T) == sizeof(uint32_t), "32-bit values only");
	uint32_t bits = 0;
	std::memcpy(&bits, &value, sizeof(bits));
	return bits;
}

std::string hex32(uint32_t value) {
	std::ostringstream s;
	s << "0x" << std::hex << value;
	return s.str();
}

// Runs one conversion over a padded, unaligned multi-row plane and compares
// every element. `generate(k)` yields the k-th input element, `convert` calls
// the public API, and `compare(input, output)` returns an empty string on a
// match or a description of the mismatch.
template <typename Src, typename Dst, typename Generate, typename Convert, typename Compare>
void check_elementwise_parity(
	uint32_t width, uint32_t height, uint32_t channels,
	Generate generate, Convert convert, Compare compare)
{
	const plane_layout src_layout{width, height, channels, sizeof(Src), sizeof(Src), 3 * sizeof(Src)};
	const plane_layout dst_layout{width, height, channels, sizeof(Dst), sizeof(Dst), 5 * sizeof(Dst)};

	std::vector<uint8_t> src(src_layout.allocation_bytes(), src_padding_sentinel);
	std::vector<uint8_t> dst(dst_layout.allocation_bytes(), padding_sentinel);

	const uint32_t row_elements = width * channels;
	std::vector<Src> inputs;
	inputs.reserve(static_cast<std::size_t>(row_elements) * height);
	uint32_t k = 0;
	for (uint32_t y = 0; y < height; ++y) {
		for (uint32_t i = 0; i < row_elements; ++i) {
			const Src value = generate(k++);
			std::memcpy(src.data() + src_layout.element_offset(y, i), &value, sizeof(Src));
			inputs.push_back(value);
		}
	}
	const std::vector<uint8_t> src_before = src;

	INFO(describe(src_layout, dst_layout));
	const auto result = convert(
		src.data() + src_layout.offset_bytes, dst.data() + dst_layout.offset_bytes,
		width, height,
		static_cast<int64_t>(src_layout.row_bytes()), static_cast<int64_t>(dst_layout.row_bytes()),
		channels);
	REQUIRE(result.ok());

	uint32_t mismatches = 0;
	std::string first_mismatch;
	for (uint32_t y = 0; y < height; ++y) {
		for (uint32_t i = 0; i < row_elements; ++i) {
			Dst output{};
			std::memcpy(&output, dst.data() + dst_layout.element_offset(y, i), sizeof(Dst));
			const std::string why = compare(inputs[static_cast<std::size_t>(y) * row_elements + i], output);
			if (!why.empty() && mismatches++ == 0) {
				std::ostringstream s;
				s << "first mismatch at row " << y << " element " << i << ": " << why;
				first_mismatch = s.str();
			}
		}
	}

	INFO(first_mismatch);
	INFO("mismatched elements: " << mismatches << " of " << inputs.size());
	CHECK(mismatches == 0);
	CHECK(src == src_before);
	CHECK(find_padding_damage(dst, dst_layout).empty());
}

template <typename Body>
void for_each_parity_geometry(Body body) {
	for (uint32_t channels = 1; channels <= 4; ++channels) {
		for (uint32_t width = 1; width * channels <= max_row_elements; ++width) {
			body(width, channels);
		}
	}
}

// ---------- References (independent of the library implementation) ----------

// IEEE 754 binary16 -> binary32 by arithmetic, not by bit manipulation.
float reference_half_to_float(uint16_t h) {
	const bool negative = (h & 0x8000u) != 0;
	const int exponent = (h >> 10) & 0x1F;
	const int mantissa = h & 0x3FF;
	float magnitude = 0.0f;
	if (exponent == 0) {
		magnitude = std::ldexp(static_cast<float>(mantissa), -24);
	} else if (exponent == 0x1F) {
		magnitude = std::numeric_limits<float>::infinity();
	} else {
		magnitude = std::ldexp(static_cast<float>(1024 + mantissa), exponent - 25);
	}
	return negative ? -magnitude : magnitude;
}

std::string compare_half_to_float(uint16_t input, float output) {
	const uint32_t got = bits_of(output);
	const bool input_is_nan = ((input >> 10) & 0x1F) == 0x1F && (input & 0x3FF) != 0;
	if (input_is_nan) {
		// Hardware converters may set the quiet bit of a signaling NaN. Require a
		// NaN with the same sign and the same payload below the quiet bit.
		const bool got_is_nan = std::isnan(output);
		const bool same_sign = ((got >> 31) & 1u) == ((input >> 15) & 1u);
		const bool same_payload = ((got >> 13) & 0x1FFu) == (input & 0x1FFu) && (got & 0x1FFFu) == 0;
		if (got_is_nan && same_sign && same_payload) {
			return {};
		}
		std::ostringstream s;
		s << "half 0x" << std::hex << input << " (NaN) -> " << hex32(got)
		  << ", expected a NaN with the same sign and payload";
		return s.str();
	}
	const uint32_t expected = bits_of(reference_half_to_float(input));
	if (got == expected) {
		return {};
	}
	std::ostringstream s;
	s << "half 0x" << std::hex << input << " -> " << hex32(got) << " (" << output
	  << "), expected " << hex32(expected) << " (" << reference_half_to_float(input) << ")";
	return s.str();
}

// ---------- Input generators ----------

// Edge values are interleaved with pseudo-random values so they land inside
// SIMD blocks at every lane position, not only in scalar tails.
uint16_t generate_uint16(uint32_t k) {
	static const uint16_t edges[] = {0x0000, 0x0001, 0x0002, 0x7FFF, 0x8000, 0x8001, 0xFFFE, 0xFFFF};
	if (k % 2 == 0) {
		return edges[(k / 2) % (sizeof(edges) / sizeof(edges[0]))];
	}
	return static_cast<uint16_t>(mix32(k));
}

uint32_t generate_uint32(uint32_t k) {
	static const uint32_t edges[] = {
		0u, 1u, 2u, 16777216u, 16777217u, 16777219u,
		0x7FFFFFBFu, 0x7FFFFFC0u, 0x7FFFFFFFu,
		0x80000000u, 0x80000001u, 0xBFFFFFFFu, 3000000000u,
		0xFFFFFF7Fu, 0xFFFFFF80u, 0xFFFFFFFFu,
	};
	if (k % 2 == 0) {
		return edges[(k / 2) % (sizeof(edges) / sizeof(edges[0]))];
	}
	return mix32(k);
}

uint16_t generate_half(uint32_t k) {
	static const uint16_t edges[] = {
		0x0000, 0x8000, 0x0001, 0x8001, 0x03FF, 0x0400, 0x3555, 0x3C00,
		0xBC00, 0x7BFF, 0xFBFF, 0x7C00, 0xFC00, 0x7E00, 0x7C01, 0xFE01,
	};
	if (k % 2 == 0) {
		return edges[(k / 2) % (sizeof(edges) / sizeof(edges[0]))];
	}
	return static_cast<uint16_t>(mix32(k));
}

} // namespace

// ---------- Compiled path report ----------

namespace {

// Mirrors the compile-time dispatch in src/common/format_convert.cpp and
// src/common/channel_swizzle.cpp. It exists only so CI logs state which paths
// the parity cases below exercised; keep it in sync when that dispatch changes.
const char *compiled_integer_path() {
#if defined(__ARM_NEON)
	return "neon";
#elif defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
	return "sse2";
#else
	return "scalar";
#endif
}

const char *compiled_half_path() {
#if NOZZLE_PLATFORM_MACOS
	return "vimage";  // falls back to the next path only if vImage reports an error
#elif defined(__F16C__)
	return "f16c";
#elif defined(__ARM_NEON) && defined(__ARM_FP16_FORMAT_IEEE)
	return "neon_fp16";
#else
	return "scalar";
#endif
}

const char *compiled_swizzle_path() {
#if NOZZLE_PLATFORM_MACOS
	return "vimage_except_rgba32_uint";
#else
	return "scalar";
#endif
}

} // namespace

TEST_CASE("SIMD parity: report compiled conversion paths", "[format_convert][simd_parity][simd_path]") {
	WARN("compiled paths: uint16_to_uint32/uint32_to_float32=" << compiled_integer_path()
		<< ", half_to_float=" << compiled_half_path()
		<< ", swizzle=" << compiled_swizzle_path());
	SUCCEED();
}

// ---------- widen_uint16_to_uint32 ----------

TEST_CASE("SIMD parity: widen_uint16_to_uint32 matches reference for every row length", "[format_convert][simd_parity]") {
	for_each_parity_geometry([](uint32_t width, uint32_t channels) {
		check_elementwise_parity<uint16_t, uint32_t>(
			width, parity_height, channels,
			[&](uint32_t k) { return generate_uint16(k + width * 131 + channels); },
			[](auto... args) { return widen_uint16_to_uint32(args...); },
			[](uint16_t input, uint32_t output) -> std::string {
				if (output == static_cast<uint32_t>(input)) {
					return {};
				}
				std::ostringstream s;
				s << "uint16 " << input << " -> " << output;
				return s.str();
			});
	});
}

// ---------- convert_uint32_to_float32 ----------

TEST_CASE("SIMD parity: convert_uint32_to_float32 matches static_cast<float> for every row length", "[format_convert][simd_parity]") {
	for_each_parity_geometry([](uint32_t width, uint32_t channels) {
		check_elementwise_parity<uint32_t, float>(
			width, parity_height, channels,
			[&](uint32_t k) { return generate_uint32(k + width * 131 + channels); },
			[](auto... args) { return convert_uint32_to_float32(args...); },
			[](uint32_t input, float output) -> std::string {
				const float expected = static_cast<float>(input);
				if (bits_of(output) == bits_of(expected)) {
					return {};
				}
				std::ostringstream s;
				s << "uint32 " << input << " -> " << output << " (" << hex32(bits_of(output))
				  << "), expected " << expected << " (" << hex32(bits_of(expected)) << ")";
				return s.str();
			});
	});
}

// ---------- widen_half_to_float ----------

TEST_CASE("SIMD parity: widen_half_to_float matches reference for every row length", "[format_convert][simd_parity]") {
	for_each_parity_geometry([](uint32_t width, uint32_t channels) {
		check_elementwise_parity<uint16_t, float>(
			width, parity_height, channels,
			[&](uint32_t k) { return generate_half(k + width * 131 + channels); },
			[](auto... args) { return widen_half_to_float(args...); },
			compare_half_to_float);
	});
}

TEST_CASE("SIMD parity: widen_half_to_float matches reference for all 65536 inputs", "[format_convert][simd_parity]") {
	// One row of every binary16 bit pattern, then the same set as four channels
	// over two rows so it also crosses a padded row boundary.
	check_elementwise_parity<uint16_t, float>(
		65536, 1, 1,
		[](uint32_t k) { return static_cast<uint16_t>(k); },
		[](auto... args) { return widen_half_to_float(args...); },
		compare_half_to_float);
	check_elementwise_parity<uint16_t, float>(
		8192, 2, 4,
		[](uint32_t k) { return static_cast<uint16_t>(k); },
		[](auto... args) { return widen_half_to_float(args...); },
		compare_half_to_float);
}

// ---------- swizzle_channels ----------

TEST_CASE("SIMD parity: swizzle_channels matches per-channel reference for every width", "[channel_swizzle][simd_parity]") {
	struct format_case {
		texture_format format;
		uint32_t bytes_per_pixel;
		const char *name;
	};
	const format_case formats[] = {
		{texture_format::rgba8_unorm, 4, "rgba8_unorm"},
		{texture_format::bgra8_unorm, 4, "bgra8_unorm"},
		{texture_format::rgba8_srgb, 4, "rgba8_srgb"},
		{texture_format::bgra8_srgb, 4, "bgra8_srgb"},
		{texture_format::rgba32_float, 16, "rgba32_float"},
		{texture_format::rgba32_uint, 16, "rgba32_uint"},
	};
	const channel_permute permutes[] = {
		{{0, 1, 2, 3}},
		permute_argb_to_bgra,
		permute_argb_to_rgba,
		permute_rgba_to_argb,
		{{2, 0, 3, 1}},
	};

	for (const auto &fmt : formats) {
		for (const auto &permute : permutes) {
			for (uint32_t width = 1; width <= max_row_elements; ++width) {
				// Channels are bytes (8-bit formats) or 32-bit words; treat a pixel as
				// four channels of channel_bytes each.
				const uint32_t channel_bytes = fmt.bytes_per_pixel / 4;
				const plane_layout src_layout{width, parity_height, 4, channel_bytes, 4, 12};
				const plane_layout dst_layout{width, parity_height, 4, channel_bytes, 4, 20};

				std::vector<uint8_t> src(src_layout.allocation_bytes(), src_padding_sentinel);
				std::vector<uint8_t> dst(dst_layout.allocation_bytes(), padding_sentinel);
				uint32_t k = width * 977 + static_cast<uint32_t>(fmt.bytes_per_pixel);
				for (uint32_t y = 0; y < parity_height; ++y) {
					for (uint32_t b = 0; b < src_layout.row_payload_bytes(); ++b) {
						src[src_layout.offset_bytes + static_cast<std::size_t>(y) * src_layout.row_bytes() + b] =
							static_cast<uint8_t>(mix32(k++));
					}
				}
				const std::vector<uint8_t> src_before = src;

				INFO("format=" << fmt.name << " permute={"
					<< int(permute.map[0]) << "," << int(permute.map[1]) << ","
					<< int(permute.map[2]) << "," << int(permute.map[3]) << "} "
					<< describe(src_layout, dst_layout));
				const auto result = swizzle_channels(
					src.data() + src_layout.offset_bytes, dst.data() + dst_layout.offset_bytes,
					width, parity_height,
					static_cast<int64_t>(src_layout.row_bytes()), static_cast<int64_t>(dst_layout.row_bytes()),
					fmt.format, permute);
				REQUIRE(result.ok());

				uint32_t mismatches = 0;
				std::string first_mismatch;
				for (uint32_t y = 0; y < parity_height; ++y) {
					for (uint32_t x = 0; x < width; ++x) {
						for (uint32_t c = 0; c < 4; ++c) {
							const std::size_t src_at = src_layout.element_offset(y, x * 4 + permute.map[c]);
							const std::size_t dst_at = dst_layout.element_offset(y, x * 4 + c);
							if (std::memcmp(src.data() + src_at, dst.data() + dst_at, channel_bytes) != 0
								&& mismatches++ == 0) {
								std::ostringstream s;
								s << "first mismatch at row " << y << " pixel " << x << " channel " << c;
								first_mismatch = s.str();
							}
						}
					}
				}

				INFO(first_mismatch);
				INFO("mismatched channels: " << mismatches);
				CHECK(mismatches == 0);
				CHECK(src == src_before);
				CHECK(find_padding_damage(dst, dst_layout).empty());
			}
		}
	}
}
