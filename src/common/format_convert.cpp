// nozzle - format_convert.cpp - CPU pixel format conversion (scalar + dispatch)

#include <nozzle/format_convert.hpp>
#include <nozzle/result.hpp>

#include "safe_math.hpp"

#include <cstring>

namespace nozzle::detail {

#if NOZZLE_PLATFORM_MACOS
Result<void> try_widen_half_to_float_vimage(
	const void *src, void *dst,
	uint32_t width, uint32_t height,
	uint32_t src_row_bytes, uint32_t dst_row_bytes,
	uint32_t channels);
#endif

#if defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
void widen_uint16_to_uint32_sse2(
	const uint8_t *src, uint8_t *dst,
	uint32_t width, uint32_t height,
	uint32_t src_row_bytes, uint32_t dst_row_bytes,
	uint32_t channels);

void convert_uint32_to_float32_sse2(
	const uint8_t *src, uint8_t *dst,
	uint32_t width, uint32_t height,
	uint32_t src_row_bytes, uint32_t dst_row_bytes,
	uint32_t channels);
#endif

#if defined(__ARM_NEON)
void widen_uint16_to_uint32_neon(
	const uint8_t *src, uint8_t *dst,
	uint32_t width, uint32_t height,
	uint32_t src_row_bytes, uint32_t dst_row_bytes,
	uint32_t channels);

void convert_uint32_to_float32_neon(
	const uint8_t *src, uint8_t *dst,
	uint32_t width, uint32_t height,
	uint32_t src_row_bytes, uint32_t dst_row_bytes,
	uint32_t channels);
#endif

} // namespace nozzle::detail

namespace nozzle {

using detail::safe_mul_u32;

static void widen_uint16_to_uint32_scalar(
	const uint8_t *src, uint8_t *dst,
	uint32_t width, uint32_t height,
	uint32_t src_row_bytes, uint32_t dst_row_bytes,
	uint32_t channels)
{
	uint64_t row_elements = static_cast<uint64_t>(width) * channels;
	for (uint32_t y = 0; y < height; ++y) {
		const auto *src_row = reinterpret_cast<const uint16_t *>(
			src + static_cast<uint64_t>(y) * src_row_bytes);
		auto *dst_row = reinterpret_cast<uint32_t *>(
			dst + static_cast<uint64_t>(y) * dst_row_bytes);
		for (uint64_t i = 0; i < row_elements; ++i) {
			dst_row[i] = static_cast<uint32_t>(src_row[i]);
		}
	}
}

static void convert_uint32_to_float32_scalar(
	const uint8_t *src, uint8_t *dst,
	uint32_t width, uint32_t height,
	uint32_t src_row_bytes, uint32_t dst_row_bytes,
	uint32_t channels)
{
	uint64_t row_elements = static_cast<uint64_t>(width) * channels;
	for (uint32_t y = 0; y < height; ++y) {
		const auto *src_row = reinterpret_cast<const uint32_t *>(
			src + static_cast<uint64_t>(y) * src_row_bytes);
		auto *dst_row = reinterpret_cast<float *>(
			dst + static_cast<uint64_t>(y) * dst_row_bytes);
		for (uint32_t i = 0; i < row_elements; ++i) {
			dst_row[i] = static_cast<float>(src_row[i]);
		}
	}
}

static float half_to_float_scalar(uint16_t h) {
	uint32_t sign = static_cast<uint32_t>(h & 0x8000) << 16;
	uint32_t exp = (h >> 10) & 0x1F;
	uint32_t mantissa = h & 0x3FF;

	if (exp == 0) {
		if (mantissa == 0) {
			uint32_t f = sign;
			float result;
			std::memcpy(&result, &f, sizeof(result));
			return result;
		}
		exp = 1;
		while ((mantissa & 0x400) == 0 && exp > 0) {
			mantissa <<= 1;
			--exp;
		}
		mantissa &= 0x3FF;
		exp = static_cast<uint32_t>(static_cast<int32_t>(exp) + 127 - 15);
	} else if (exp == 0x1F) {
		uint32_t f = sign | (0xFFu << 23) | (mantissa << 13);
		float result;
		std::memcpy(&result, &f, sizeof(result));
		return result;
	} else {
		exp = exp - 15 + 127;
	}

	uint32_t f = sign | (exp << 23) | (mantissa << 13);
	float result;
	std::memcpy(&result, &f, sizeof(result));
	return result;
}

static void widen_half_to_float_scalar(
	const uint8_t *src, uint8_t *dst,
	uint32_t width, uint32_t height,
	uint32_t src_row_bytes, uint32_t dst_row_bytes,
	uint32_t channels)
{
	uint64_t row_elements = static_cast<uint64_t>(width) * channels;
	for (uint32_t y = 0; y < height; ++y) {
		const auto *src_row = reinterpret_cast<const uint16_t *>(
			src + static_cast<uint64_t>(y) * src_row_bytes);
		auto *dst_row = reinterpret_cast<float *>(
			dst + static_cast<uint64_t>(y) * dst_row_bytes);
		for (uint64_t i = 0; i < row_elements; ++i) {
			dst_row[i] = half_to_float_scalar(src_row[i]);
		}
	}
}

Result<void> widen_uint16_to_uint32(
	const void *src, void *dst,
	uint32_t width, uint32_t height,
	uint32_t src_row_bytes, uint32_t dst_row_bytes,
	uint32_t channels)
{
	if (!src || !dst) return Error{ErrorCode::InvalidArgument, "null pointer"};
	if (width == 0 || height == 0) return Error{ErrorCode::InvalidArgument, "zero dimensions"};

	uint32_t min_src{};
	uint32_t min_dst{};
	uint32_t row_el{};
	if (!safe_mul_u32(width, channels, row_el))
		return Error{ErrorCode::InvalidArgument, "width * channels overflow"};
	if (!safe_mul_u32(row_el, sizeof(uint16_t), min_src))
		return Error{ErrorCode::InvalidArgument, "row bytes overflow"};
	if (!safe_mul_u32(row_el, sizeof(uint32_t), min_dst))
		return Error{ErrorCode::InvalidArgument, "row bytes overflow"};
	if (src_row_bytes < min_src || dst_row_bytes < min_dst)
		return Error{ErrorCode::InvalidArgument, "row_bytes too small"};

	auto *s = static_cast<const uint8_t *>(src);
	auto *d = static_cast<uint8_t *>(dst);

#if defined(__ARM_NEON)
	detail::widen_uint16_to_uint32_neon(s, d, width, height, src_row_bytes, dst_row_bytes, channels);
#elif defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
	detail::widen_uint16_to_uint32_sse2(s, d, width, height, src_row_bytes, dst_row_bytes, channels);
#else
	widen_uint16_to_uint32_scalar(s, d, width, height, src_row_bytes, dst_row_bytes, channels);
#endif
	return {};
}

Result<void> convert_uint32_to_float32(
	const void *src, void *dst,
	uint32_t width, uint32_t height,
	uint32_t src_row_bytes, uint32_t dst_row_bytes,
	uint32_t channels)
{
	if (!src || !dst) return Error{ErrorCode::InvalidArgument, "null pointer"};
	if (width == 0 || height == 0) return Error{ErrorCode::InvalidArgument, "zero dimensions"};

	uint32_t min_row{};
	uint32_t row_el{};
	if (!safe_mul_u32(width, channels, row_el))
		return Error{ErrorCode::InvalidArgument, "width * channels overflow"};
	if (!safe_mul_u32(row_el, sizeof(uint32_t), min_row))
		return Error{ErrorCode::InvalidArgument, "row bytes overflow"};
	if (src_row_bytes < min_row || dst_row_bytes < min_row)
		return Error{ErrorCode::InvalidArgument, "row_bytes too small"};

	auto *s = static_cast<const uint8_t *>(src);
	auto *d = static_cast<uint8_t *>(dst);

#if defined(__ARM_NEON)
	detail::convert_uint32_to_float32_neon(s, d, width, height, src_row_bytes, dst_row_bytes, channels);
#elif defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
	detail::convert_uint32_to_float32_sse2(s, d, width, height, src_row_bytes, dst_row_bytes, channels);
#else
	convert_uint32_to_float32_scalar(s, d, width, height, src_row_bytes, dst_row_bytes, channels);
#endif
	return {};
}

Result<void> widen_half_to_float(
	const void *src, void *dst,
	uint32_t width, uint32_t height,
	uint32_t src_row_bytes, uint32_t dst_row_bytes,
	uint32_t channels)
{
	if (!src || !dst) return Error{ErrorCode::InvalidArgument, "null pointer"};
	if (width == 0 || height == 0) return Error{ErrorCode::InvalidArgument, "zero dimensions"};

	uint32_t min_src{};
	uint32_t min_dst{};
	uint32_t row_el{};
	if (!safe_mul_u32(width, channels, row_el))
		return Error{ErrorCode::InvalidArgument, "width * channels overflow"};
	if (!safe_mul_u32(row_el, sizeof(uint16_t), min_src))
		return Error{ErrorCode::InvalidArgument, "row bytes overflow"};
	if (!safe_mul_u32(row_el, sizeof(float), min_dst))
		return Error{ErrorCode::InvalidArgument, "row bytes overflow"};
	if (src_row_bytes < min_src || dst_row_bytes < min_dst)
		return Error{ErrorCode::InvalidArgument, "row_bytes too small"};

#if NOZZLE_PLATFORM_MACOS
	auto r = detail::try_widen_half_to_float_vimage(
		src, dst, width, height, src_row_bytes, dst_row_bytes, channels);
	if (r.ok()) return r;
#endif

	widen_half_to_float_scalar(
		static_cast<const uint8_t *>(src), static_cast<uint8_t *>(dst),
		width, height, src_row_bytes, dst_row_bytes, channels);
	return {};
}

} // namespace nozzle
