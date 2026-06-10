#pragma once

#include <nozzle/result.hpp>
#include <nozzle/types.hpp>

#include <cstdint>

namespace nozzle {

constexpr uint32_t backend_capabilities_version{1};

constexpr uint64_t texture_format_bit(texture_format format) noexcept {
    auto value = static_cast<uint32_t>(format);
    if (value == 0 || 63 < value) {
        return 0;
    }
    return 1ull << value;
}

constexpr bool supports_format(uint64_t format_bits, texture_format format) noexcept {
    auto bit = texture_format_bit(format);
    return bit != 0 && (format_bits & bit) != 0;
}

Result<backend_capabilities> get_backend_capabilities(backend_type backend);

bool is_backend_available(backend_type backend) noexcept;

const char *backend_type_name(backend_type backend) noexcept;

} // namespace nozzle
