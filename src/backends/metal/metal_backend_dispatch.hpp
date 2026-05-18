// nozzle - metal_backend_dispatch.hpp - Metal backend dispatch implementation

#pragma once

#include "metal_helpers.hpp"
#include <nozzle/backends/metal.hpp>

namespace nozzle {
namespace detail {
namespace backend {

inline auto get_default_device() -> void * {
    return metal::get_default_mtl_device();
}

inline auto create_ring_texture(void *device, uint32_t width, uint32_t height, uint32_t format, uint32_t /*slot_index*/) -> Result<texture> {
    auto pair_result = metal::create_iosurface_texture(device, width, height, format);
    if (!pair_result.ok()) {
        return pair_result.error();
    }
    auto &pair = pair_result.value();
    native_format_desc native{};
    native.backend = backend_type::metal;
    native.kind = native_format_kind::mtl_pixel_format;
    native.value = pair.mtl_pixel_format;
    return make_texture_from_backend(pair.mtl_texture, pair.io_surface, pair.width, pair.height, pair.pixel_format, 0, &native);
}

inline auto get_shared_resource_id(const texture &tex) -> uint64_t {
    void *surface = get_surface_native(tex);
    if (!surface) return detail::kInvalidSharedResourceId;
    return static_cast<uint64_t>(metal::get_iosurface_id(surface));
}

inline auto get_native_surface(const texture &tex) -> void * {
    return get_surface_native(tex);
}

inline void release_texture_resources(void *native_texture, void *native_surface) {
    metal::release_mtl_texture_resources(native_texture, native_surface);
}

inline auto lookup_texture(void * /*device*/, uint64_t shared_id, uint32_t width, uint32_t height, uint32_t format, uint8_t channel_swizzle, uint32_t semantic_format, uint8_t native_format_kind, uint32_t native_format_value, uint64_t /*native_modifier*/, uint32_t /*native_plane_count*/, const uint32_t * /*native_plane_strides*/, const uint32_t * /*native_plane_offsets*/, uint64_t /*sender_pid*/, const char * /*sender_uuid*/) -> Result<texture> {
    return metal::lookup_iosurface_texture(
        static_cast<uint32_t>(shared_id),
        width,
        height,
        format,
        channel_swizzle,
        semantic_format,
        native_format_kind,
        native_format_value);
}

inline auto get_backend_type() -> backend_type {
    return backend_type::metal;
}

inline auto get_native_texture(const texture &tex) -> void * {
    return get_texture_native(tex);
}

inline auto blit_textures(void *device, void *src, void *dst, uint32_t width, uint32_t height) -> Result<void> {
    return metal::blit_to_texture(device, src, dst, width, height);
}

inline auto signal_texture_ready(void * /*native_texture*/, uint32_t /*slot_index*/) -> Result<void> {
    return {};
}

inline auto wait_for_texture(void * /*native_texture*/, uint32_t /*slot_index*/, uint32_t /*timeout_ms*/) -> Result<void> {
    return {};
}

inline void release_texture_sync(void * /*native_texture*/, uint32_t /*slot_index*/) {}

inline auto validate_texture_device(void *device, void *native_texture) -> Result<void> {
    return metal::check_texture_device(device, native_texture);
}

inline auto notify_sender_uuid(const char * /*uuid*/) -> Result<void> { return {}; }
inline void cleanup_sender_socket() {}
inline void release_device(void *device) { metal::release_mtl_device(device); }

} // namespace backend
} // namespace detail
} // namespace nozzle
