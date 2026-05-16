#pragma once

#include <cstddef>
#include <cstdint>
#include <nozzle/result.hpp>
#include <nozzle/types.hpp>

namespace nozzle {

// Widen uint16 buffer to uint32 buffer. No normalization, no type conversion.
// Each source row is read at src_row_bytes stride, each dest row written at dst_row_bytes stride.
// element_count = width * height * channels (caller computes based on image dimensions).
Result<void> widen_uint16_to_uint32(
	const void *src, void *dst,
	uint32_t width, uint32_t height,
	int64_t src_row_bytes, int64_t dst_row_bytes,
	uint32_t channels);

// Convert uint32 buffer to float32 buffer.
// Each source row is read at src_row_bytes stride, each dest row written at dst_row_bytes stride.
// element_count = width * height * channels.
Result<void> convert_uint32_to_float32(
	const void *src, void *dst,
	uint32_t width, uint32_t height,
	int64_t src_row_bytes, int64_t dst_row_bytes,
	uint32_t channels);

// Widen half-float (IEEE 754-2008 binary16) buffer to float32 buffer.
// Each source row is read at src_row_bytes stride, each dest row written at dst_row_bytes stride.
// element_count = width * height * channels.
Result<void> widen_half_to_float(
	const void *src, void *dst,
	uint32_t width, uint32_t height,
	int64_t src_row_bytes, int64_t dst_row_bytes,
	uint32_t channels);

// Fill the alpha channel of a 4-channel pixel buffer with the canonical opaque value.
// Only whitelisted 4-channel formats are accepted — all others return UnsupportedFormat.
// row_stride_bytes is signed to support negative strides (bottom-up layouts).
Result<void> fill_opaque_alpha_channel(
	void *data,
	uint32_t width,
	uint32_t height,
	std::ptrdiff_t row_stride_bytes,
	texture_format storage_format);

} // namespace nozzle
