#pragma once

// Internal helpers for Metal backend — only included from .mm files.

#include <nozzle/types.hpp>
#include <nozzle/result.hpp>
#include <nozzle/texture.hpp>
#include <nozzle/device.hpp>

namespace nozzle::metal {

struct metal_texture_pair {
    void *mtl_texture{nullptr};  // id<MTLTexture>, owned
    void *io_surface{nullptr};   // IOSurfaceRef, owned
    uint32_t io_surface_id{0};
    uint32_t width{0};
    uint32_t height{0};
    uint32_t pixel_format{0};    // observed nozzle format as uint32_t
    uint32_t fourcc{0};          // IOSurface FourCC (actual observed)
    uint32_t mtl_pixel_format{0}; // MTLPixelFormat used for creation
};

Result<metal_texture_pair> create_iosurface_texture(
    void *mtl_device,
    uint32_t width,
    uint32_t height,
    uint32_t pixel_format
);

uint32_t get_iosurface_id(void *io_surface);

void release_mtl_device(void *device);

void *get_default_mtl_device();

void release_mtl_texture_resources(void *texture, void *surface);

uint32_t nozzle_format_to_mtl(uint32_t nozzle_format);
bool mtl_format_to_iosurface(uint32_t mtl_format, uint32_t &out_pf, uint32_t &out_bpe);

Result<texture> lookup_iosurface_texture(
    uint32_t iosurface_id,
    uint32_t width,
    uint32_t height,
    uint32_t format,
    uint8_t channel_swizzle = 0);

bool is_iosurface_backed(void *mtl_texture);
void *get_io_surface_from_texture(void *mtl_texture);

Result<void> blit_to_texture(void *native_device, void *src_texture, void *dst_texture, uint32_t width, uint32_t height);
Result<void> blit_from_texture(void *native_device, void *src_texture, void *dst_texture, uint32_t width, uint32_t height);

Result<void> swap_rb_channels(void *mtl_texture_ptr, uint32_t width, uint32_t height);

} // namespace nozzle::metal

namespace nozzle::detail {

device make_device_from_backend(void *backend_ptr);

texture make_texture_from_backend(
    void *backend_texture,
    void *backend_surface,
    uint32_t width,
    uint32_t height,
    uint32_t pixel_format,
    uint8_t channel_swizzle,
    const native_format_desc *native_desc
);

void *get_texture_native(const texture &t);
void *get_surface_native(const texture &t);

} // namespace nozzle::detail
