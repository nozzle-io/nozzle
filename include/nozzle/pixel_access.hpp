#pragma once

#include <nozzle/types.hpp>
#include <nozzle/result.hpp>
#include <nozzle/frame.hpp>

namespace nozzle {

struct mapped_pixels {
    void *data{nullptr};
    uint32_t row_bytes{0};
    uint32_t width{0};
    uint32_t height{0};
    texture_format format{texture_format::unknown};
};

Result<mapped_pixels> lock_frame_pixels(const frame &frm);
void unlock_frame_pixels(const frame &frm);

Result<mapped_pixels> lock_writable_pixels(writable_frame &frm);
void unlock_writable_pixels(writable_frame &frm);

} // namespace nozzle
