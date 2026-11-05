#include <array>
#include <cstdint>
#include <iostream>
#include <memory>
#include <vector>

#if !defined(__F16C__)
#error "The focused F16C regression must be compiled with F16C enabled"
#endif

namespace nozzle::detail {

void widen_half_to_float_f16c(
	const uint8_t *src, uint8_t *dst,
	uint32_t width, uint32_t height,
	uint32_t src_row_bytes, uint32_t dst_row_bytes,
	uint32_t channels);

} // namespace nozzle::detail

namespace {

constexpr float output_sentinel = -999.0f;
constexpr std::array<uint16_t, 8> half_values = {
	0x3c00, 0x4000, 0x4200, 0x4400,
	0x4500, 0x4600, 0x4700, 0x4800,
};

uint16_t half_value(uint32_t index) {
	return half_values[index % half_values.size()];
}

float expected_value(uint32_t index) {
	return static_cast<float>((index % half_values.size()) + 1);
}

bool check_single_row(uint32_t elements) {
	auto src = std::make_unique<uint16_t[]>(elements);
	for (uint32_t i = 0; i < elements; ++i) {
		src[i] = half_value(i);
	}

	constexpr uint32_t padding_elements = 3;
	std::vector<float> dst(elements + padding_elements, output_sentinel);
	nozzle::detail::widen_half_to_float_f16c(
		reinterpret_cast<const uint8_t *>(src.get()),
		reinterpret_cast<uint8_t *>(dst.data()),
		elements, 1,
		elements * sizeof(uint16_t),
		static_cast<uint32_t>(dst.size() * sizeof(float)),
		1);

	bool ok = true;
	for (uint32_t i = 0; i < elements; ++i) {
		if (dst[i] != expected_value(i)) {
			std::cerr << elements << "-element row: output[" << i << "]="
				<< dst[i] << ", expected " << expected_value(i) << '\n';
			ok = false;
		}
	}
	for (uint32_t i = elements; i < dst.size(); ++i) {
		if (dst[i] != output_sentinel) {
			std::cerr << elements << "-element row: padding[" << i << "] changed\n";
			ok = false;
		}
	}
	for (uint32_t i = 0; i < elements; ++i) {
		if (src[i] != half_value(i)) {
			std::cerr << elements << "-element row: source[" << i << "] changed\n";
			ok = false;
		}
	}
	return ok;
}

bool check_two_padded_rows() {
	constexpr uint32_t width = 9;
	constexpr uint32_t height = 2;
	constexpr uint32_t src_stride = 12;
	constexpr uint32_t dst_stride = 13;

	std::vector<uint16_t> src(height * src_stride, 0xdead);
	for (uint32_t y = 0; y < height; ++y) {
		for (uint32_t x = 0; x < width; ++x) {
			src[y * src_stride + x] = half_value(y * width + x);
		}
	}
	const auto original_src = src;
	std::vector<float> dst(height * dst_stride, output_sentinel);

	nozzle::detail::widen_half_to_float_f16c(
		reinterpret_cast<const uint8_t *>(src.data()),
		reinterpret_cast<uint8_t *>(dst.data()),
		width, height,
		src_stride * sizeof(uint16_t),
		dst_stride * sizeof(float),
		1);

	bool ok = true;
	for (uint32_t y = 0; y < height; ++y) {
		for (uint32_t x = 0; x < width; ++x) {
			const float expected = expected_value(y * width + x);
			if (dst[y * dst_stride + x] != expected) {
				std::cerr << "padded row " << y << ": output[" << x << "]="
					<< dst[y * dst_stride + x] << ", expected " << expected << '\n';
				ok = false;
			}
		}
		for (uint32_t x = width; x < dst_stride; ++x) {
			if (dst[y * dst_stride + x] != output_sentinel) {
				std::cerr << "padded row " << y << ": padding[" << x << "] changed\n";
				ok = false;
			}
		}
	}
	if (src != original_src) {
		std::cerr << "padded rows: source or source padding changed\n";
		ok = false;
	}
	return ok;
}

} // namespace

int main() {
	std::cout << "__F16C__=" << __F16C__ << '\n';
	bool ok = true;
	for (uint32_t elements : {4u, 7u, 8u, 9u}) {
		ok = check_single_row(elements) && ok;
	}
	ok = check_two_padded_rows() && ok;
	if (!ok) return 1;
	std::cout << "F16C numerical regression passed\n";
	return 0;
}
