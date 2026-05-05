// nozzle - format_convert_vimage.cpp - macOS vImage half-float widening

#if NOZZLE_PLATFORM_MACOS

#include <nozzle/format_convert.hpp>
#include <nozzle/result.hpp>

#include <Accelerate/Accelerate.h>

namespace nozzle::detail {

Result<void> try_widen_half_to_float_vimage(
	const void *src, void *dst,
	uint32_t width, uint32_t height,
	uint32_t src_row_bytes, uint32_t dst_row_bytes,
	uint32_t channels)
{
	uint32_t row_elements = width * channels;

	vImage_Buffer src_buf{};
	src_buf.data = const_cast<void *>(src);
	src_buf.height = height;
	src_buf.width = row_elements;
	src_buf.rowBytes = src_row_bytes;

	vImage_Buffer dst_buf{};
	dst_buf.data = dst;
	dst_buf.height = height;
	dst_buf.width = row_elements;
	dst_buf.rowBytes = dst_row_bytes;

	vImage_Error err = vImageConvert_Planar16FtoPlanarF(&src_buf, &dst_buf, kvImageNoFlags);
	if (err != kvImageNoError) {
		return Error{ErrorCode::BackendError, "vImageConvert_Planar16FtoPlanarF failed"};
	}

	return {};
}

} // namespace nozzle::detail

#endif
