#pragma once

// Internal helpers for Metal backend — only included from .mm files.

#include <bbb/nozzle/types.hpp>
#include <bbb/nozzle/result.hpp>
#include <bbb/nozzle/texture.hpp>
#include <bbb/nozzle/device.hpp>

namespace bbb::nozzle::metal {

struct metal_texture_pair {
    void *mtl_texture{nullptr};  // id<MTLTexture>, owned
    void *io_surface{nullptr};   // IOSurfaceRef, owned
    uint32_t io_surface_id{0};
    uint32_t width{0};
    uint32_t height{0};
    uint32_t pixel_format{0};    // MTLPixelFormat as uint32_t
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
    uint32_t format);

} // namespace bbb::nozzle::metal

namespace bbb::nozzle::detail {

device make_device_from_backend(void *backend_ptr);

texture make_texture_from_backend(
    void *backend_texture,
    void *backend_surface,
    uint32_t width,
    uint32_t height,
    uint32_t pixel_format
);

void *get_texture_native(const texture &t);
void *get_surface_native(const texture &t);

} // namespace bbb::nozzle::detail
