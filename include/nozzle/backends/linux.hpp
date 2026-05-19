#pragma once

#if !defined(NOZZLE_HAS_DMA_BUF)
#error "DMA-BUF backend not available on this platform"
#endif

#include <nozzle/types.hpp>
#include <nozzle/texture.hpp>
#include <nozzle/device.hpp>

namespace nozzle::dma_buf {

struct device_desc {
    void *gbm_device{nullptr};
};

Result<device> wrap_device(const device_desc &desc);

struct texture_wrap_desc {
    void *egl_image{nullptr};
    int dmabuf_fd{-1};
    uint32_t fourcc{0};
    uint64_t modifier{0};
    uint32_t width{0};
    uint32_t height{0};
    uint32_t plane_count{0};
    uint32_t plane_strides[4]{0};
    uint32_t plane_offsets[4]{0};
};

Result<texture> wrap_texture(const texture_wrap_desc &desc);

struct plane_desc {
    uint32_t stride{0};
    uint32_t offset{0};
};

struct publish_desc {
    int dmabuf_fd{-1};
    uint32_t width{0};
    uint32_t height{0};
    uint32_t fourcc{0};
    uint64_t modifier{0};
    uint32_t plane_count{0};
    plane_desc planes[4]{};
    texture_format storage_format{texture_format::unknown};
    texture_format semantic_format{texture_format::unknown};
    channel_swizzle swizzle{channel_swizzle::identity};
};

void *get_egl_image(const texture &tex);
int get_dmabuf_fd(const texture &tex);

} // namespace nozzle::dma_buf
