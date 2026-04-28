// nozzle - backend_dispatch.hpp - Unified backend operations

#pragma once

#include <bbb/nozzle/types.hpp>
#include <bbb/nozzle/result.hpp>
#include <bbb/nozzle/texture.hpp>

namespace bbb::nozzle {
namespace detail {
namespace backend {

auto get_default_device() -> void *;
auto create_ring_texture(void *device, uint32_t width, uint32_t height, uint32_t format) -> Result<texture>;
auto get_shared_resource_id(const texture &tex) -> uint64_t;
auto get_native_surface(const texture &tex) -> void *;
void release_texture_resources(void *native_texture, void *native_surface);
auto lookup_texture(void *device, uint64_t shared_id, uint32_t width, uint32_t height, uint32_t format) -> Result<texture>;
auto wrap_backend_texture(void *backend_texture, void *backend_surface, uint32_t width, uint32_t height, uint32_t pixel_format) -> texture;
auto get_backend_type() -> backend_type;

} // namespace backend
} // namespace detail
} // namespace bbb::nozzle

#if NOZZLE_HAS_METAL
    #include "metal/metal_backend_dispatch.hpp"
#elif NOZZLE_HAS_D3D11
    #include "d3d11/d3d11_backend_dispatch.hpp"
#endif
