#pragma once

#if !defined(NOZZLE_HAS_METAL)
#error "Metal backend not available on this platform"
#endif

#include <nozzle/types.hpp>
#include <nozzle/texture.hpp>
#include <nozzle/device.hpp>

using mtl_device_handle = void *;
using mtl_texture_handle = void *;
using surface_handle = void *;
using pixel_format_value = uint32_t;
using surface_id = uint32_t;

namespace nozzle::metal {

struct device_desc {
    mtl_device_handle device{nullptr};
};

Result<device> wrap_device(const device_desc &desc);

struct texture_wrap_desc {
    mtl_texture_handle texture{nullptr};
    surface_handle io_surface{nullptr};
    pixel_format_value format{0};
    uint32_t width{0};
    uint32_t height{0};
};

Result<texture> wrap_texture(const texture_wrap_desc &desc);

mtl_texture_handle get_texture(const texture &tex);
surface_handle get_io_surface(const texture &tex);

surface_handle get_io_surface_from_native_texture(mtl_texture_handle native_texture);
bool is_native_texture_iosurface_backed(mtl_texture_handle native_texture);

} // namespace nozzle::metal
