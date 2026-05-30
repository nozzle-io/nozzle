#pragma once

#include <cstddef>
#include <memory>
#include <nozzle/types.hpp>
#include <nozzle/result.hpp>
#include <nozzle/frame.hpp>

namespace nozzle {

struct mapped_pixels {
    void *data{nullptr};
    std::ptrdiff_t row_stride_bytes{0};
    uint32_t width{0};
    uint32_t height{0};
    texture_format format{texture_format::unknown};
    texture_origin origin{texture_origin::top_left};
    cpu_layout_desc cpu_layout{};
    format_source source{format_source::requested};
};

class pixel_mapping {
public:
    struct Impl;

    pixel_mapping() noexcept;
    explicit pixel_mapping(std::unique_ptr<Impl> impl) noexcept;
    ~pixel_mapping() noexcept;

    pixel_mapping(const pixel_mapping &) = delete;
    pixel_mapping &operator=(const pixel_mapping &) = delete;
    pixel_mapping(pixel_mapping &&other) noexcept;
    pixel_mapping &operator=(pixel_mapping &&other) noexcept;

    bool valid() const noexcept;
    bool writable() const noexcept;
    const mapped_pixels &pixels() const noexcept;

    Result<void> unlock_checked() noexcept;
    void unlock() noexcept;

private:
    friend Result<pixel_mapping> lock_frame_pixels_mapping_with_origin(const frame &, texture_origin);
    friend Result<pixel_mapping> lock_writable_pixels_mapping_with_origin(writable_frame &, texture_origin);

    std::unique_ptr<Impl> impl_;
};

Result<pixel_mapping> lock_frame_pixels_mapping_with_origin(const frame &frm, texture_origin desired_origin);
Result<pixel_mapping> lock_writable_pixels_mapping_with_origin(writable_frame &frm, texture_origin desired_origin);

// Legacy frame-level APIs retained for compatibility. Prefer the
// pixel_mapping-returning APIs for explicit, mapping-owned cleanup.
Result<mapped_pixels> lock_frame_pixels_with_origin(const frame &frm, texture_origin desired_origin);
void unlock_frame_pixels(const frame &frm);

Result<mapped_pixels> lock_writable_pixels_with_origin(writable_frame &frm, texture_origin desired_origin);
Result<void> unlock_writable_pixels_checked(writable_frame &frm);
void unlock_writable_pixels(writable_frame &frm);

} // namespace nozzle
