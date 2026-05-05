// nozzle - format_convert_neon.cpp - ARM NEON SIMD pixel format conversion

#if defined(__ARM_NEON)

#include <nozzle/format_convert.hpp>
#include <nozzle/result.hpp>
#include <arm_neon.h>

namespace nozzle::detail {

void widen_uint16_to_uint32_neon(
	const uint8_t *src, uint8_t *dst,
	uint32_t width, uint32_t height,
	uint32_t src_row_bytes, uint32_t dst_row_bytes,
	uint32_t channels)
{
	const uint32_t row_elements = width * channels;
	for (uint32_t y = 0; y < height; ++y) {
		const auto *src_row = reinterpret_cast<const uint16_t *>(
			src + static_cast<uint64_t>(y) * src_row_bytes);
		auto *dst_row = reinterpret_cast<uint32_t *>(
			dst + static_cast<uint64_t>(y) * dst_row_bytes);
		uint32_t i = 0;
		for (; i + 4 <= row_elements; i += 4) {
			vst1q_u32(dst_row + i, vmovl_u16(vld1_u16(src_row + i)));
		}
		for (; i < row_elements; ++i) {
			dst_row[i] = static_cast<uint32_t>(src_row[i]);
		}
	}
}

void convert_uint32_to_float32_neon(
	const uint8_t *src, uint8_t *dst,
	uint32_t width, uint32_t height,
	uint32_t src_row_bytes, uint32_t dst_row_bytes,
	uint32_t channels)
{
	const uint32_t row_elements = width * channels;
	for (uint32_t y = 0; y < height; ++y) {
		const auto *src_row = reinterpret_cast<const uint32_t *>(
			src + static_cast<uint64_t>(y) * src_row_bytes);
		auto *dst_row = reinterpret_cast<float *>(
			dst + static_cast<uint64_t>(y) * dst_row_bytes);
		uint32_t i = 0;
		for (; i + 4 <= row_elements; i += 4) {
			vst1q_f32(dst_row + i, vcvtq_f32_u32(vld1q_u32(src_row + i)));
		}
		for (; i < row_elements; ++i) {
			dst_row[i] = static_cast<float>(src_row[i]);
		}
	}
}

} // namespace nozzle::detail

#endif
