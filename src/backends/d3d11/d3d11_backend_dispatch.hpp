// nozzle - d3d11_backend_dispatch.hpp - D3D11 backend dispatch implementation

#pragma once

#include "d3d11_helpers.hpp"
#include <nozzle/backends/d3d11.hpp>

namespace nozzle {
namespace detail {
namespace backend {

inline auto get_default_device() -> void * {
    return d3d11::get_default_d3d11_device();
}

inline auto create_ring_texture(void *device, uint32_t width, uint32_t height, uint32_t format) -> Result<texture> {
    return d3d11::create_shared_texture(device, width, height, format);
}

inline auto get_shared_resource_id(const texture &tex) -> uint64_t {
    void *handle = d3d11::get_shared_handle(tex);
    if (!handle) return 0;
    return reinterpret_cast<uint64_t>(handle);
}

inline auto get_native_surface(const texture &tex) -> void * {
    return d3d11::get_shared_handle(tex);
}

inline void release_texture_resources(void *native_texture, void *native_surface) {
    d3d11::release_d3d11_texture_resources(native_texture, native_surface);
}

inline auto lookup_texture(void *device, uint64_t shared_id, uint32_t width, uint32_t height, uint32_t format) -> Result<texture> {
    return d3d11::lookup_shared_texture(device, shared_id, width, height, format);
}

inline auto wrap_backend_texture(void *backend_texture, void *backend_surface, uint32_t width, uint32_t height, uint32_t pixel_format) -> texture {
    return make_texture_from_backend(backend_texture, backend_surface, width, height, pixel_format);
}

inline auto get_backend_type() -> backend_type {
    return backend_type::d3d11;
}

inline auto get_native_texture(const texture &tex) -> void * {
    return get_texture_native(tex);
}

inline auto is_native_texture_shared(void * /*native_texture*/) -> bool {
    return false;
}

inline auto get_native_surface_from_texture(void * /*native_texture*/) -> void * {
    return nullptr;
}

inline auto blit_textures(void * /*device*/, void *src, void *dst, uint32_t /*width*/, uint32_t /*height*/) -> Result<void> {
    return d3d11::blit_to_texture(src, dst);
}

} // namespace backend
} // namespace detail
} // namespace nozzle
