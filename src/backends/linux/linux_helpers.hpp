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

struct dmabuf_plane {
    uint32_t stride{0};
    uint32_t offset{0};
};

struct dmabuf_allocation {
    int fd{-1};
    uint32_t plane_count{0};
    dmabuf_plane planes[4]{};
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
    uint32_t plane_count,
    const dmabuf_plane *planes,
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
    uint32_t plane_count,
    const dmabuf_plane *planes,
    uint64_t modifier
);

} // namespace nozzle::detail::linux_backend

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
