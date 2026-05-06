#pragma once

#include <cstdint>

namespace nozzle::detail {

inline bool safe_mul_u32(uint32_t a, uint32_t b, uint32_t &result) noexcept {
	if (a == 0 || b == 0) { result = 0; return true; }
	if (a > UINT32_MAX / b) return false;
	result = a * b;
	return true;
}

} // namespace nozzle::detail
