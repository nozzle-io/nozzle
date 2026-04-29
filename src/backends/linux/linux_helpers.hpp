#pragma once

#if NOZZLE_HAS_DMA_BUF

#include <nozzle/result.hpp>
#include <nozzle/texture.hpp>
#include <nozzle/device.hpp>
#include <cstdint>

struct gbm_bo;
struct gbm_device;
struct _EGLDisplay;

namespace nozzle::detail::linux_backend {
class dmabuf_texture_cache;

struct dmabuf_allocation {
    int fd{-1};
    uint32_t stride{0};
    uint64_t modifier{0};
    void *gbm_bo{nullptr};
};

int open_drm_render_node();

void *create_gbm_device(int drm_fd);

Result<dmabuf_allocation> allocate_dmabuf(
    void *gbm_dev,
    uint32_t width,
    uint32_t height,
    uint32_t format
);

void *import_egl_image(
    void *egl_display,
    int fd,
    uint32_t width,
    uint32_t height,
    uint32_t fourcc,
    uint32_t stride,
    uint64_t modifier
);

void destroy_egl_image(void *egl_display, void *egl_image);

void destroy_gbm_bo(void *gbm_bo);

void close_drm_fd(int fd);

void destroy_gbm_device(void *gbm_dev);

void destroy_dmabuf_allocation(dmabuf_allocation &alloc);

uint32_t drm_format_from_nozzle(uint32_t nozzle_format);

void *get_egl_display();

void *get_default_gbm_device();

void release_linux_device(void *device);

Result<texture> create_dmabuf_texture(
    void *device,
    uint32_t width,
    uint32_t height,
    uint32_t format
);

Result<texture> import_dmabuf_texture(
    int fd,
    uint32_t width,
    uint32_t height,
    uint32_t fourcc,
    uint32_t stride,
    uint64_t modifier
);

Result<texture> lookup_dmabuf_texture(
    uint32_t slot_index,
    uint32_t width,
    uint32_t height,
    uint32_t format
);

Result<texture> lookup_dmabuf_texture_with_fds(
    const char *sender_uuid,
    uint32_t slot_index,
    uint32_t ring_size,
    uint32_t width,
    uint32_t height,
    uint32_t format,
    dmabuf_texture_cache &cache
);

} // namespace nozzle::detail::linux_backend

namespace nozzle::detail {

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

} // namespace nozzle::detail

#endif
