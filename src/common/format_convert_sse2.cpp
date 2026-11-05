// nozzle - format_convert_sse2.cpp - SSE2 SIMD pixel format conversion

#if defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)

#include <nozzle/format_convert.hpp>
#include <nozzle/result.hpp>
#include <emmintrin.h>

namespace nozzle::detail {

void widen_uint16_to_uint32_sse2(
	const uint8_t *src, uint8_t *dst,
	uint32_t width, uint32_t height,
	uint32_t src_row_bytes, uint32_t dst_row_bytes,
	uint32_t channels)
{
	const uint32_t row_elements = width * channels;
	const __m128i zero = _mm_setzero_si128();
	for (uint32_t y = 0; y < height; ++y) {
		const auto *src_row = reinterpret_cast<const uint16_t *>(
			src + static_cast<uint64_t>(y) * src_row_bytes);
		auto *dst_row = reinterpret_cast<uint32_t *>(
			dst + static_cast<uint64_t>(y) * dst_row_bytes);
		uint32_t i = 0;
		for (; i + 8 <= row_elements; i += 8) {
			__m128i v16 = _mm_loadu_si128(reinterpret_cast<const __m128i *>(src_row + i));
			_mm_storeu_si128(reinterpret_cast<__m128i *>(dst_row + i),
				_mm_unpacklo_epi16(v16, zero));
			_mm_storeu_si128(reinterpret_cast<__m128i *>(dst_row + i + 4),
				_mm_unpackhi_epi16(v16, zero));
		}
		for (; i + 4 <= row_elements; i += 4) {
			__m128i v16 = _mm_loadl_epi64(reinterpret_cast<const __m128i *>(src_row + i));
			_mm_storeu_si128(reinterpret_cast<__m128i *>(dst_row + i),
				_mm_unpacklo_epi16(v16, zero));
		}
		for (; i < row_elements; ++i) {
			dst_row[i] = static_cast<uint32_t>(src_row[i]);
		}
	}
}

void convert_uint32_to_float32_sse2(
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
			__m128i vi = _mm_loadu_si128(reinterpret_cast<const __m128i *>(src_row + i));
			_mm_storeu_ps(dst_row + i, _mm_cvtepi32_ps(vi));
		}
		for (; i < row_elements; ++i) {
			dst_row[i] = static_cast<float>(src_row[i]);
		}
	}
}

} // namespace nozzle::detail

#endif
