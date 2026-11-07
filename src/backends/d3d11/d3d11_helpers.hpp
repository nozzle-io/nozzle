#pragma once

#if NOZZLE_HAS_D3D11

#include <nozzle/types.hpp>
#include <nozzle/result.hpp>
#include <nozzle/texture.hpp>
#include <nozzle/device.hpp>

struct IDXGIKeyedMutex;

namespace nozzle::d3d11 {

enum class mutex_acquire_result {
    acquired,
    timeout,
    abandoned,
    failed,
};

class keyed_mutex_lease {
public:
    keyed_mutex_lease() noexcept = default;
    ~keyed_mutex_lease() noexcept;
    keyed_mutex_lease(const keyed_mutex_lease &) = delete;
    keyed_mutex_lease &operator=(const keyed_mutex_lease &) = delete;
    keyed_mutex_lease(keyed_mutex_lease &&other) noexcept;
    keyed_mutex_lease &operator=(keyed_mutex_lease &&other) noexcept;

    // An already-held lease is unchanged and returns failed.
    mutex_acquire_result acquire(void *texture, uint32_t timeout_ms = 0) noexcept;
    // Consumes ownership even on failure; an empty lease releases successfully.
    Result<void> release() noexcept;
    bool held() const noexcept;

private:
    long release_native() noexcept;
    IDXGIKeyedMutex *mutex_{nullptr};
};

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
    uint32_t format,
    uint32_t semantic_format = 0,
    uint64_t sender_pid = 0
);

void release_d3d11_texture_resources(void *texture, void *shared_handle);

void *get_default_d3d11_device();
void release_d3d11_device(void *device);
bool d3d11_supports_format(void *device, uint32_t format, uint32_t usage);

bool signal_slot_ready(void *shared_texture, uint32_t slot_index);
void signal_slot_done(void *shared_texture, uint32_t slot_index);
// Success leaves the mutex held until release_slot, not merely until return.
Result<void> wait_for_slot_checked(void *shared_texture, uint32_t slot_index, uint32_t timeout_ms);
bool wait_for_slot(void *shared_texture, uint32_t slot_index, uint32_t timeout_ms);
void release_slot(void *shared_texture, uint32_t slot_index);
Result<void> blit_to_texture(void *src_texture, void *dst_texture);
// Caller owns the destination lease through subsequent metadata publication.
// This helper never acquires or releases synchronization on either texture.
Result<void> blit_to_texture_with_write_lease(void *src_texture, void *dst_texture);
Result<void> blit_from_texture(void *src_texture, void *dst_texture);

} // namespace nozzle::d3d11

namespace nozzle::detail {

device make_device_from_backend(void *backend_ptr);

texture make_texture_from_backend(
    void *backend_texture,
    void *backend_surface,
    uint32_t width,
    uint32_t height,
    uint32_t pixel_format,
    uint8_t channel_swizzle,
    const native_format_desc *native_desc,
    uint32_t semantic_format_val
);

void *get_texture_native(const texture &t);
void *get_surface_native(const texture &t);

} // namespace nozzle::detail

#endif
