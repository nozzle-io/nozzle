#pragma once

#if NOZZLE_HAS_D3D11

#include <bbb/nozzle/types.hpp>
#include <bbb/nozzle/result.hpp>
#include <bbb/nozzle/texture.hpp>
#include <bbb/nozzle/device.hpp>

namespace bbb::nozzle::d3d11 {

Result<texture> create_shared_texture(
    void *d3d11_device,
    uint32_t width,
    uint32_t height,
    uint32_t format
);

Result<texture> lookup_shared_texture(
    void *d3d11_device,
    uint64_t shared_handle,
    uint32_t width,
    uint32_t height,
    uint32_t format
);

void release_d3d11_texture_resources(void *texture, void *shared_handle);

void *get_default_d3d11_device();
void release_d3d11_device(void *device);
bool d3d11_supports_format(void *device, uint32_t format, uint32_t usage);

void signal_slot_ready(void *shared_texture, uint32_t slot_index);
void signal_slot_done(void *shared_texture, uint32_t slot_index);
bool wait_for_slot(void *shared_texture, uint32_t slot_index, uint32_t timeout_ms);
void release_slot(void *shared_texture, uint32_t slot_index);

} // namespace bbb::nozzle::d3d11

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

#endif
