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

inline auto create_ring_texture(void *device, uint32_t width, uint32_t height, uint32_t format, uint32_t /*slot_index*/) -> Result<texture> {
    return d3d11::create_shared_texture(device, width, height, format);
}

inline auto get_shared_resource_id(const texture &tex) -> uint64_t {
    void *handle = d3d11::get_shared_handle(tex);
    if (!handle) return detail::kInvalidSharedResourceId;
    return reinterpret_cast<uint64_t>(handle);
}

inline auto get_native_surface(const texture &tex) -> void * {
    return d3d11::get_shared_handle(tex);
}

inline void release_texture_resources(void *native_texture, void *native_surface) {
    d3d11::release_d3d11_texture_resources(native_texture, native_surface);
}

inline auto lookup_texture(void *device, uint64_t shared_id, uint32_t width, uint32_t height, uint32_t format, uint8_t, uint32_t semantic_format, uint8_t /*native_format_kind*/, uint32_t /*native_format_value*/, uint64_t /*native_modifier*/, uint32_t /*native_plane_count*/, const uint32_t * /*native_plane_strides*/, const uint32_t * /*native_plane_offsets*/, uint64_t sender_pid, const char * /*sender_uuid*/) -> Result<texture> {
    return d3d11::lookup_shared_texture(device, shared_id, width, height, format, semantic_format, sender_pid);
}

inline auto get_backend_type() -> backend_type {
    return backend_type::d3d11;
}

inline auto get_native_texture(const texture &tex) -> void * {
    return get_texture_native(tex);
}

inline auto blit_textures(void * /*device*/, void *src, void *dst, uint32_t /*width*/, uint32_t /*height*/) -> Result<void> {
    return d3d11::blit_to_texture(src, dst);
}

inline auto validate_texture_device(void * /*device*/, void * /*native_texture*/) -> Result<void> { return {}; }

inline auto notify_sender_uuid(const char * /*uuid*/) -> Result<void> { return {}; }
inline void cleanup_sender_socket() {}
inline void release_device(void *device) { d3d11::release_d3d11_device(device); }

} // namespace backend
} // namespace detail
} // namespace nozzle
