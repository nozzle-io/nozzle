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

#if defined(__ARM_NEON) && defined(__ARM_FP16_FORMAT_IEEE)
void widen_half_to_float_neon(
	const uint8_t *src, uint8_t *dst,
	uint32_t width, uint32_t height,
	uint32_t src_row_bytes, uint32_t dst_row_bytes,
	uint32_t channels);
#endif

#if defined(__F16C__)
void widen_half_to_float_f16c(
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
	int64_t src_row_bytes, int64_t dst_row_bytes,
	uint32_t channels)
{
	uint64_t row_elements = static_cast<uint64_t>(width) * channels;
	for (uint32_t y = 0; y < height; ++y) {
		const auto *src_row = reinterpret_cast<const uint16_t *>(
			src + static_cast<int64_t>(y) * src_row_bytes);
		auto *dst_row = reinterpret_cast<uint32_t *>(
			dst + static_cast<int64_t>(y) * dst_row_bytes);
		for (uint64_t i = 0; i < row_elements; ++i) {
			dst_row[i] = static_cast<uint32_t>(src_row[i]);
		}
	}
}

static void convert_uint32_to_float32_scalar(
	const uint8_t *src, uint8_t *dst,
	uint32_t width, uint32_t height,
	int64_t src_row_bytes, int64_t dst_row_bytes,
	uint32_t channels)
{
	uint64_t row_elements = static_cast<uint64_t>(width) * channels;
	for (uint32_t y = 0; y < height; ++y) {
		const auto *src_row = reinterpret_cast<const uint32_t *>(
			src + static_cast<int64_t>(y) * src_row_bytes);
		auto *dst_row = reinterpret_cast<float *>(
			dst + static_cast<int64_t>(y) * dst_row_bytes);
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
		int shift = 0;
		while ((mantissa & 0x400) == 0) {
			mantissa <<= 1;
			++shift;
		}
		mantissa &= 0x3FF;
		exp = static_cast<uint32_t>(113 - shift);
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
	int64_t src_row_bytes, int64_t dst_row_bytes,
	uint32_t channels)
{
	uint64_t row_elements = static_cast<uint64_t>(width) * channels;
	for (uint32_t y = 0; y < height; ++y) {
		const auto *src_row = reinterpret_cast<const uint16_t *>(
			src + static_cast<int64_t>(y) * src_row_bytes);
		auto *dst_row = reinterpret_cast<float *>(
			dst + static_cast<int64_t>(y) * dst_row_bytes);
		for (uint64_t i = 0; i < row_elements; ++i) {
			dst_row[i] = half_to_float_scalar(src_row[i]);
		}
	}
}

Result<void> widen_uint16_to_uint32(
	const void *src, void *dst,
	uint32_t width, uint32_t height,
	int64_t src_row_bytes, int64_t dst_row_bytes,
	uint32_t channels)
{
	if (!src || !dst) return Error{ErrorCode::InvalidArgument, "null pointer"};
	if (width == 0 || height == 0) return Error{ErrorCode::InvalidArgument, "zero dimensions"};
	if (src_row_bytes < 0) return Error{ErrorCode::InvalidArgument, "src_row_bytes must be non-negative"};
	if (dst_row_bytes < 0) return Error{ErrorCode::InvalidArgument, "dst_row_bytes must be non-negative"};

	uint32_t min_src{};
	uint32_t min_dst{};
	uint32_t row_el{};
	if (!safe_mul_u32(width, channels, row_el))
		return Error{ErrorCode::InvalidArgument, "width * channels overflow"};
	if (!safe_mul_u32(row_el, sizeof(uint16_t), min_src))
		return Error{ErrorCode::InvalidArgument, "row bytes overflow"};
	if (!safe_mul_u32(row_el, sizeof(uint32_t), min_dst))
		return Error{ErrorCode::InvalidArgument, "row bytes overflow"};
	if (src_row_bytes < static_cast<int64_t>(min_src) || dst_row_bytes < static_cast<int64_t>(min_dst))
		return Error{ErrorCode::InvalidArgument, "row_bytes too small"};

	auto *s = static_cast<const uint8_t *>(src);
	auto *d = static_cast<uint8_t *>(dst);

#if defined(__ARM_NEON)
	detail::widen_uint16_to_uint32_neon(s, d, width, height,
		static_cast<uint32_t>(src_row_bytes), static_cast<uint32_t>(dst_row_bytes), channels);
#elif defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
	detail::widen_uint16_to_uint32_sse2(s, d, width, height,
		static_cast<uint32_t>(src_row_bytes), static_cast<uint32_t>(dst_row_bytes), channels);
#else
	widen_uint16_to_uint32_scalar(s, d, width, height, src_row_bytes, dst_row_bytes, channels);
#endif
	return {};
}

Result<void> convert_uint32_to_float32(
	const void *src, void *dst,
	uint32_t width, uint32_t height,
	int64_t src_row_bytes, int64_t dst_row_bytes,
	uint32_t channels)
{
	if (!src || !dst) return Error{ErrorCode::InvalidArgument, "null pointer"};
	if (width == 0 || height == 0) return Error{ErrorCode::InvalidArgument, "zero dimensions"};
	if (src_row_bytes < 0) return Error{ErrorCode::InvalidArgument, "src_row_bytes must be non-negative"};
	if (dst_row_bytes < 0) return Error{ErrorCode::InvalidArgument, "dst_row_bytes must be non-negative"};

	uint32_t min_row{};
	uint32_t row_el{};
	if (!safe_mul_u32(width, channels, row_el))
		return Error{ErrorCode::InvalidArgument, "width * channels overflow"};
	if (!safe_mul_u32(row_el, sizeof(uint32_t), min_row))
		return Error{ErrorCode::InvalidArgument, "row bytes overflow"};
	if (src_row_bytes < static_cast<int64_t>(min_row) || dst_row_bytes < static_cast<int64_t>(min_row))
		return Error{ErrorCode::InvalidArgument, "row_bytes too small"};

	auto *s = static_cast<const uint8_t *>(src);
	auto *d = static_cast<uint8_t *>(dst);

#if defined(__ARM_NEON)
	detail::convert_uint32_to_float32_neon(s, d, width, height,
		static_cast<uint32_t>(src_row_bytes), static_cast<uint32_t>(dst_row_bytes), channels);
#elif defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
	detail::convert_uint32_to_float32_sse2(s, d, width, height,
		static_cast<uint32_t>(src_row_bytes), static_cast<uint32_t>(dst_row_bytes), channels);
#else
	convert_uint32_to_float32_scalar(s, d, width, height, src_row_bytes, dst_row_bytes, channels);
#endif
	return {};
}

Result<void> widen_half_to_float(
	const void *src, void *dst,
	uint32_t width, uint32_t height,
	int64_t src_row_bytes, int64_t dst_row_bytes,
	uint32_t channels)
{
	if (!src || !dst) return Error{ErrorCode::InvalidArgument, "null pointer"};
	if (width == 0 || height == 0) return Error{ErrorCode::InvalidArgument, "zero dimensions"};
	if (src_row_bytes < 0) return Error{ErrorCode::InvalidArgument, "src_row_bytes must be non-negative"};
	if (dst_row_bytes < 0) return Error{ErrorCode::InvalidArgument, "dst_row_bytes must be non-negative"};

	uint32_t min_src{};
	uint32_t min_dst{};
	uint32_t row_el{};
	if (!safe_mul_u32(width, channels, row_el))
		return Error{ErrorCode::InvalidArgument, "width * channels overflow"};
	if (!safe_mul_u32(row_el, sizeof(uint16_t), min_src))
		return Error{ErrorCode::InvalidArgument, "row bytes overflow"};
	if (!safe_mul_u32(row_el, sizeof(float), min_dst))
		return Error{ErrorCode::InvalidArgument, "row bytes overflow"};
	if (src_row_bytes < static_cast<int64_t>(min_src) || dst_row_bytes < static_cast<int64_t>(min_dst))
		return Error{ErrorCode::InvalidArgument, "row_bytes too small"};

#if NOZZLE_PLATFORM_MACOS
	auto r = detail::try_widen_half_to_float_vimage(
		src, dst, width, height,
		static_cast<uint32_t>(src_row_bytes), static_cast<uint32_t>(dst_row_bytes), channels);
	if (r.ok()) return r;
#endif

	const auto *s = static_cast<const uint8_t *>(src);
	auto *d = static_cast<uint8_t *>(dst);

#if defined(__F16C__)
	detail::widen_half_to_float_f16c(s, d, width, height,
		static_cast<uint32_t>(src_row_bytes), static_cast<uint32_t>(dst_row_bytes), channels);
#elif defined(__ARM_NEON) && defined(__ARM_FP16_FORMAT_IEEE)
	detail::widen_half_to_float_neon(s, d, width, height,
		static_cast<uint32_t>(src_row_bytes), static_cast<uint32_t>(dst_row_bytes), channels);
#else
	widen_half_to_float_scalar(s, d, width, height, src_row_bytes, dst_row_bytes, channels);
#endif
	return {};
}

static uint32_t alpha_byte_offset_for_format(texture_format fmt) {
	switch (fmt) {
	case texture_format::rgba8_unorm:
	case texture_format::bgra8_unorm:
		return 3;
	case texture_format::rgba16_unorm:
	case texture_format::rgba16_float:
		return 6;
	case texture_format::rgba32_float:
	case texture_format::rgba32_uint:
		return 12;
	default:
		return 0;
	}
}

static uint32_t bpp_for_alpha_format(texture_format fmt) {
	switch (fmt) {
	case texture_format::rgba8_unorm:
	case texture_format::bgra8_unorm:
		return 4;
	case texture_format::rgba16_unorm:
	case texture_format::rgba16_float:
		return 8;
	case texture_format::rgba32_float:
	case texture_format::rgba32_uint:
		return 16;
	default:
		return 0;
	}
}

static void write_opaque_alpha(uint8_t *pixel, uint32_t alpha_offset, texture_format fmt) {
	switch (fmt) {
	case texture_format::rgba8_unorm:
	case texture_format::bgra8_unorm:
		pixel[alpha_offset] = 0xFF;
		break;
	case texture_format::rgba16_unorm: {
		uint16_t val = 0xFFFF;
		std::memcpy(pixel + alpha_offset, &val, sizeof(val));
		break;
	}
	case texture_format::rgba16_float: {
		uint16_t val = 0x3C00;
		std::memcpy(pixel + alpha_offset, &val, sizeof(val));
		break;
	}
	case texture_format::rgba32_float: {
		float val = 1.0f;
		std::memcpy(pixel + alpha_offset, &val, sizeof(val));
		break;
	}
	case texture_format::rgba32_uint: {
		uint32_t val = 1u;
		std::memcpy(pixel + alpha_offset, &val, sizeof(val));
		break;
	}
	default:
		break;
	}
}

Result<void> fill_opaque_alpha_channel(
	void *data,
	uint32_t width,
	uint32_t height,
	std::ptrdiff_t row_stride_bytes,
	texture_format storage_format)
{
	if (!data) return Error{ErrorCode::InvalidArgument, "null data"};
	if (width == 0 || height == 0) return Error{ErrorCode::InvalidArgument, "zero dimensions"};
	if (row_stride_bytes == 0) return Error{ErrorCode::InvalidArgument, "zero row stride"};

	uint32_t bpp = bpp_for_alpha_format(storage_format);
	if (bpp == 0) return Error{ErrorCode::UnsupportedFormat, "format not supported for alpha fill"};

	uint32_t alpha_offset = alpha_byte_offset_for_format(storage_format);

	uint32_t min_row{};
	if (!safe_mul_u32(width, bpp, min_row))
		return Error{ErrorCode::InvalidArgument, "width * bpp overflow"};

	std::ptrdiff_t abs_stride = row_stride_bytes;
	if (abs_stride < 0) {
		if (abs_stride == PTRDIFF_MIN) return Error{ErrorCode::InvalidArgument, "row stride is PTRDIFF_MIN"};
		abs_stride = -abs_stride;
	}
	if (abs_stride < static_cast<std::ptrdiff_t>(min_row))
		return Error{ErrorCode::InvalidArgument, "row stride too small for width and format"};

	auto *base = static_cast<uint8_t *>(data);
	for (uint32_t y = 0; y < height; ++y) {
		uint8_t *row = base + static_cast<std::ptrdiff_t>(y) * row_stride_bytes;
		for (uint32_t x = 0; x < width; ++x) {
			write_opaque_alpha(row + x * bpp, alpha_offset, storage_format);
		}
	}
	return {};
}

} // namespace nozzle
