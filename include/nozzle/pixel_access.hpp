#pragma once

#include <cstddef>
#include <nozzle/types.hpp>
#include <nozzle/result.hpp>
#include <nozzle/frame.hpp>

namespace nozzle {

struct mapped_pixels {
    void *data{nullptr};
    std::ptrdiff_t row_stride_bytes{0};  // positive = downward, negative = upward
    uint32_t width{0};
    uint32_t height{0};
    texture_format format{texture_format::unknown};
    texture_origin origin{texture_origin::top_left};
};

Result<mapped_pixels> lock_frame_pixels_with_origin(const frame &frm, texture_origin desired_origin);
void unlock_frame_pixels(const frame &frm);

Result<mapped_pixels> lock_writable_pixels_with_origin(writable_frame &frm, texture_origin desired_origin);
void unlock_writable_pixels(writable_frame &frm);

} // namespace nozzle
