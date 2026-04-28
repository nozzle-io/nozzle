#pragma once

#if !defined(NOZZLE_HAS_METAL)
#error "Metal backend not available on this platform"
#endif

#include <bbb/nozzle/types.hpp>
#include <bbb/nozzle/texture.hpp>
#include <bbb/nozzle/device.hpp>

using MTLDeviceHandle = void *;
using MTLTextureHandle = void *;
using SurfaceHandle = void *;
using PixelFormatValue = uint32_t;
using SurfaceID = uint32_t;

namespace bbb::nozzle::metal {

struct DeviceDesc {
    MTLDeviceHandle device{nullptr};
};

Result<device> wrap_device(const DeviceDesc &desc);

struct TextureWrapDesc {
    MTLTextureHandle texture{nullptr};
    SurfaceHandle io_surface{nullptr};
    PixelFormatValue format{0};
    uint32_t width{0};
    uint32_t height{0};
};

Result<texture> wrap_texture(const TextureWrapDesc &desc);

MTLTextureHandle get_texture(const texture &tex);
SurfaceHandle get_io_surface(const texture &tex);

} // namespace bbb::nozzle::metal
