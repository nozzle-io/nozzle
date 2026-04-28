// nozzle - metal_backend.mm - macOS Metal backend (IOSurface-backed shared textures)

#import <Metal/Metal.h>

#include <bbb/nozzle/backends/metal.hpp>
#include <bbb/nozzle/result.hpp>
#include <bbb/nozzle/types.hpp>
#include "metal_helpers.hpp"

namespace bbb::nozzle::metal {

void *get_default_mtl_device() {
    @autoreleasepool {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        // MTLCreateSystemDefaultDevice follows "Create Rule" — caller owns the reference.
        return (void *)device;
    }
}

bool metal_supports_format(void *device_ptr, uint32_t format, uint32_t usage) {
    (void)usage;
    @autoreleasepool {
        id<MTLDevice> device = (id<MTLDevice>)device_ptr;
        if (!device) {
            return false;
        }

        auto fmt = static_cast<TextureFormat>(format);
        if (fmt == TextureFormat::Unknown) {
            return false;
        }

        return true;
    }
}

void release_mtl_device(void *device_ptr) {
    @autoreleasepool {
        if (device_ptr) {
            id<MTLDevice> device = (id<MTLDevice>)device_ptr;
            [device release];
        }
    }
}

Result<device> wrap_device(const DeviceDesc &desc) {
    if (!desc.device) {
        return Error{ErrorCode::InvalidArgument, "DeviceDesc.device is null"};
    }
    @autoreleasepool {
        id<MTLDevice> mtl_device = (id<MTLDevice>)desc.device;
        [mtl_device retain];
        void *ptr = (void *)mtl_device;
        return detail::make_device_from_backend(ptr);
    }
}

} // namespace bbb::nozzle::metal
