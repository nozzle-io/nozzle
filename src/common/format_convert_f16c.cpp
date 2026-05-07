// nozzle - format_convert_f16c.cpp - x86 F16C half-to-float SIMD conversion

#if defined(__F16C__)

#include <nozzle/format_convert.hpp>
#include <nozzle/result.hpp>
#include <immintrin.h>
#include <cstring>

namespace nozzle::detail {

void widen_half_to_float_f16c(
	const uint8_t *src, uint8_t *dst,
	uint32_t width, uint32_t height,
	uint32_t src_row_bytes, uint32_t dst_row_bytes,
	uint32_t channels)
{
	const uint32_t row_elements = width * channels;
	for (uint32_t y = 0; y < height; ++y) {
		const auto *src_row = reinterpret_cast<const uint16_t *>(
			src + static_cast<uint64_t>(y) * src_row_bytes);
		auto *dst_row = reinterpret_cast<float *>(
			dst + static_cast<uint64_t>(y) * dst_row_bytes);
		uint32_t i = 0;
		for (; i + 8 <= row_elements; i += 8) {
			__m128i h = _mm_loadu_si128(reinterpret_cast<const __m128i *>(src_row + i));
			_mm_storeu_ps(dst_row + i, _mm_cvtph_ps(h));
		}
		for (; i < row_elements; ++i) {
			uint32_t h = src_row[i];
			uint32_t sign = (h >> 15) << 31;
			uint32_t exp = (h >> 10) & 0x1F;
			uint32_t mantissa = h & 0x3FF;
			uint32_t f;
			if (exp == 0) {
				if (mantissa == 0) {
					f = sign;
				} else {
					int shift = 0;
					while ((mantissa & 0x400) == 0) { mantissa <<= 1; ++shift; }
					mantissa &= 0x3FF;
					f = sign | ((113u - static_cast<uint32_t>(shift)) << 23) | (mantissa << 13);
				}
			} else if (exp == 31) {
				f = sign | (0xFFu << 23) | (mantissa << 13);
			} else {
				f = sign | ((exp - 15 + 127) << 23) | (mantissa << 13);
			}
			std::memcpy(&dst_row[i], &f, sizeof(float));
		}
	}
}

} // namespace nozzle::detail

#endif
