// nozzle - channel_swizzle.cpp - CPU pixel channel permutation

#include <nozzle/channel_swizzle.hpp>
#include <nozzle/result.hpp>
#include <nozzle/types.hpp>

#include "safe_math.hpp"

#include <cstring>

#if NOZZLE_PLATFORM_MACOS
namespace nozzle::detail {
Result<void> try_swizzle_vimage(
	const void *src, void *dst,
	uint32_t width, uint32_t height,
	uint32_t src_row_bytes, uint32_t dst_row_bytes,
	uint32_t bytes_per_pixel,
	channel_permute permute);
}
#endif

namespace nozzle {

using detail::safe_mul_u32;

static uint32_t bytes_per_pixel_for_format(texture_format fmt) {
	switch (fmt) {
		case texture_format::rgba8_unorm:
		case texture_format::bgra8_unorm:
		case texture_format::rgba8_srgb:
		case texture_format::bgra8_srgb:
			return 4;
		case texture_format::rgba32_float:
		case texture_format::rgba32_uint:
			return 16;
		default:
			return 0;
	}
}

static void swizzle_scalar(
	const uint8_t *src, uint8_t *dst,
	uint32_t width, uint32_t height,
	int64_t src_row_bytes, int64_t dst_row_bytes,
	uint32_t bpp,
	channel_permute permute)
{
	for (uint32_t y = 0; y < height; ++y) {
		const uint8_t *src_row = src + static_cast<int64_t>(y) * src_row_bytes;
		uint8_t *dst_row = dst + static_cast<int64_t>(y) * dst_row_bytes;
		for (uint32_t x = 0; x < width; ++x) {
			const uint8_t *sp = src_row + static_cast<uint64_t>(x) * bpp;
			uint8_t *dp = dst_row + static_cast<uint64_t>(x) * bpp;
			for (uint32_t c = 0; c < 4; ++c) {
				uint32_t src_byte = static_cast<uint32_t>(permute.map[c]) * (bpp / 4);
				uint32_t dst_byte = c * (bpp / 4);
				std::memcpy(dp + dst_byte, sp + src_byte, bpp / 4);
			}
		}
	}
}

Result<void> swizzle_channels(
	const void *src, void *dst,
	uint32_t width, uint32_t height,
	int64_t src_row_bytes, int64_t dst_row_bytes,
	texture_format format,
	channel_permute permute)
{
	if (!src || !dst) {
		return Error{ErrorCode::InvalidArgument, "src and dst must not be null"};
	}
	if (width == 0 || height == 0) {
		return Error{ErrorCode::InvalidArgument, "dimensions must be non-zero"};
	}

	uint32_t bpp = bytes_per_pixel_for_format(format);
	if (bpp == 0) {
		return Error{ErrorCode::UnsupportedFormat, "unsupported texture format for swizzle"};
	}

	if (src_row_bytes < 0) return Error{ErrorCode::InvalidArgument, "src_row_bytes must be non-negative"};
	if (dst_row_bytes < 0) return Error{ErrorCode::InvalidArgument, "dst_row_bytes must be non-negative"};
	if (src_row_bytes > UINT32_MAX) return Error{ErrorCode::InvalidArgument, "src_row_bytes exceeds uint32_t range"};
	if (dst_row_bytes > UINT32_MAX) return Error{ErrorCode::InvalidArgument, "dst_row_bytes exceeds uint32_t range"};

	int64_t min_row = static_cast<int64_t>(width) * bpp;
	if (src_row_bytes < min_row || dst_row_bytes < min_row)
		return Error{ErrorCode::InvalidArgument, "row_bytes too small for width and format"};

#if NOZZLE_PLATFORM_MACOS
	// Keep rgba32_uint on the scalar path. Using ARGBFFFF for uint lanes
	// would preserve bytes today but would encode a false float contract here.
	const bool use_vimage = format != texture_format::rgba32_uint;
	if (use_vimage) {
		auto vimage_result = detail::try_swizzle_vimage(
			src, dst, width, height,
			static_cast<uint32_t>(src_row_bytes), static_cast<uint32_t>(dst_row_bytes),
			bpp, permute);
		if (vimage_result.ok()) {
			return vimage_result;
		}
	}
#endif

	swizzle_scalar(
		static_cast<const uint8_t *>(src),
		static_cast<uint8_t *>(dst),
		width, height,
		src_row_bytes, dst_row_bytes,
		bpp, permute);

	return {};
}

} // namespace nozzle
