#pragma once

#include <cstdint>
#include <nozzle/result.hpp>

namespace nozzle {

// Widen uint16 buffer to uint32 buffer. No normalization, no type conversion.
// Each source row is read at src_row_bytes stride, each dest row written at dst_row_bytes stride.
// element_count = width * height * channels (caller computes based on image dimensions).
Result<void> widen_uint16_to_uint32(
	const void *src, void *dst,
	uint32_t width, uint32_t height,
	uint32_t src_row_bytes, uint32_t dst_row_bytes,
	uint32_t channels);

// Convert uint32 buffer to float32 buffer.
// Each source row is read at src_row_bytes stride, each dest row written at dst_row_bytes stride.
// element_count = width * height * channels.
Result<void> convert_uint32_to_float32(
	const void *src, void *dst,
	uint32_t width, uint32_t height,
	uint32_t src_row_bytes, uint32_t dst_row_bytes,
	uint32_t channels);

// Widen half-float (IEEE 754-2008 binary16) buffer to float32 buffer.
// Each source row is read at src_row_bytes stride, each dest row written at dst_row_bytes stride.
// element_count = width * height * channels.
Result<void> widen_half_to_float(
	const void *src, void *dst,
	uint32_t width, uint32_t height,
	uint32_t src_row_bytes, uint32_t dst_row_bytes,
	uint32_t channels);

} // namespace nozzle
