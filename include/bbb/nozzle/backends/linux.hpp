#pragma once

#if !defined(NOZZLE_HAS_DMA_BUF)
#error "DMA-BUF backend not available on this platform"
#endif

#include <bbb/nozzle/types.hpp>
#include <bbb/nozzle/texture.hpp>
#include <bbb/nozzle/device.hpp>

namespace bbb::nozzle::dma_buf {

struct device_desc {
    void *gbm_device{nullptr};
};

Result<device> wrap_device(const device_desc &desc);

struct texture_wrap_desc {
    void *egl_image{nullptr};
    int dmabuf_fd{-1};
    uint32_t fourcc{0};
    uint32_t stride{0};
    uint64_t modifier{0};
    uint32_t width{0};
    uint32_t height{0};
};

Result<texture> wrap_texture(const texture_wrap_desc &desc);

void *get_egl_image(const texture &tex);
int get_dmabuf_fd(const texture &tex);

} // namespace bbb::nozzle::dma_buf
