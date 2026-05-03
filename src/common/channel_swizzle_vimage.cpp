// nozzle - channel_swizzle_vimage.cpp - macOS vImage channel permutation

#if NOZZLE_PLATFORM_MACOS

#include <nozzle/channel_swizzle.hpp>
#include <nozzle/result.hpp>

#include <Accelerate/Accelerate.h>

namespace nozzle::detail {

Result<void> try_swizzle_vimage(
	const void *src, void *dst,
	uint32_t width, uint32_t height,
	uint32_t src_row_bytes, uint32_t dst_row_bytes,
	uint32_t bytes_per_pixel,
	channel_permute permute)
{
	vImage_Buffer src_buf{};
	src_buf.data = const_cast<void *>(src);
	src_buf.height = height;
	src_buf.width = width;
	src_buf.rowBytes = src_row_bytes;

	vImage_Buffer dst_buf{};
	dst_buf.data = dst;
	dst_buf.height = height;
	dst_buf.width = width;
	dst_buf.rowBytes = dst_row_bytes;

	vImage_Error err;

	if (bytes_per_pixel == 4) {
		err = vImagePermuteChannels_ARGB8888(&src_buf, &dst_buf, permute.map, kvImageNoFlags);
	} else if (bytes_per_pixel == 16) {
		err = vImagePermuteChannels_ARGBFFFF(&src_buf, &dst_buf, permute.map, kvImageNoFlags);
	} else {
		return Error{ErrorCode::UnsupportedFormat, "unsupported bytes_per_pixel for vImage"};
	}

	if (err != kvImageNoError) {
		return Error{ErrorCode::BackendError, "vImagePermuteChannels failed"};
	}

	return {};
}

} // namespace nozzle::detail

#endif
