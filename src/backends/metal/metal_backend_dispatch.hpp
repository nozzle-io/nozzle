// nozzle - metal_backend_dispatch.hpp - Metal backend dispatch implementation

#pragma once

#include "metal_helpers.hpp"
#include <bbb/nozzle/backends/metal.hpp>

namespace bbb::nozzle {
namespace detail {
namespace backend {

inline auto get_default_device() -> void * {
    return metal::get_default_mtl_device();
}

inline auto create_ring_texture(void *device, uint32_t width, uint32_t height, uint32_t format) -> Result<texture> {
    auto pair_result = metal::create_iosurface_texture(device, width, height, format);
    if (!pair_result.ok()) {
        return pair_result.error();
    }
    auto &pair = pair_result.value();
    return make_texture_from_backend(pair.mtl_texture, pair.io_surface, pair.width, pair.height, pair.pixel_format);
}

inline auto get_shared_resource_id(const texture &tex) -> uint64_t {
    void *surface = get_surface_native(tex);
    if (!surface) return 0;
    return static_cast<uint64_t>(metal::get_iosurface_id(surface));
}

inline auto get_native_surface(const texture &tex) -> void * {
    return get_surface_native(tex);
}

inline void release_texture_resources(void *native_texture, void *native_surface) {
    metal::release_mtl_texture_resources(native_texture, native_surface);
}

inline auto lookup_texture(void * /*device*/, uint64_t shared_id, uint32_t width, uint32_t height, uint32_t format) -> Result<texture> {
    return metal::lookup_iosurface_texture(
        static_cast<uint32_t>(shared_id), width, height, format);
}

inline auto wrap_backend_texture(void *backend_texture, void *backend_surface, uint32_t width, uint32_t height, uint32_t pixel_format) -> texture {
    return make_texture_from_backend(backend_texture, backend_surface, width, height, pixel_format);
}

inline auto get_backend_type() -> backend_type {
    return backend_type::metal;
}

} // namespace backend
} // namespace detail
} // namespace bbb::nozzle
