#pragma once

#include <string_view>

#include <nozzle/result.hpp>

#include "shared_state.hpp"

namespace nozzle {
namespace detail {

// Names are UTF-8 strings stored in NUL-terminated protocol fields.
inline bool is_valid_name_utf8(std::string_view text) {
    std::size_t index{0};
    while (index < text.size()) {
        const auto lead = static_cast<unsigned char>(text[index]);
        std::size_t length{0};
        if (0 < lead && lead <= 0x7F) length = 1;
        else if (0xC2 <= lead && lead <= 0xDF) length = 2;
        else if (0xE0 <= lead && lead <= 0xEF) length = 3;
        else if (0xF0 <= lead && lead <= 0xF4) length = 4;
        else return false;

        if (text.size() - index < length) return false;
        for (std::size_t offset = 1; offset < length; ++offset) {
            const auto byte = static_cast<unsigned char>(text[index + offset]);
            if ((byte & 0xC0) != 0x80) return false;
        }
        if (1 < length) {
            const auto second = static_cast<unsigned char>(text[index + 1]);
            if ((lead == 0xE0 && second < 0xA0) ||
                (lead == 0xED && 0xA0 <= second) ||
                (lead == 0xF0 && second < 0x90) ||
                (lead == 0xF4 && 0x90 <= second)) return false;
        }
        index += length;
    }
    return true;
}

// Requires valid UTF-8; payload_limit excludes the trailing NUL.
inline std::size_t utf8_prefix_length(std::string_view text, std::size_t payload_limit) {
    if (text.size() <= payload_limit) return text.size();
    auto length = payload_limit;
    while (0 < length && (static_cast<unsigned char>(text[length]) & 0xC0) == 0x80) {
        --length;
    }
    return length;
}

inline Result<void> validate_sender_name(std::string_view name) {
    if (name.empty() || sizeof(SenderSharedState::name) <= name.size() ||
        !is_valid_name_utf8(name)) {
        return Error{ErrorCode::InvalidArgument,
            "sender name must be 1 to 63 bytes of valid UTF-8 without embedded NUL"};
    }
    return {};
}

inline Result<void> validate_application_name(std::string_view name) {
    if (!is_valid_name_utf8(name)) {
        return Error{ErrorCode::InvalidArgument,
            "application name must be valid UTF-8 without embedded NUL"};
    }
    return {};
}

} // namespace detail
} // namespace nozzle
