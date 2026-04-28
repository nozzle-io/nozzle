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
#if __has_feature(objc_arc)
        return (__bridge_retained void *)device;
#else
        return (void *)device;
#endif
    }
}

bool metal_supports_format(void *device_ptr, uint32_t format, uint32_t usage) {
    (void)usage;
    @autoreleasepool {
#if __has_feature(objc_arc)
        id<MTLDevice> device = (__bridge id<MTLDevice>)device_ptr;
#else
        id<MTLDevice> device = (id<MTLDevice>)device_ptr;
#endif
        if (!device) {
            return false;
        }

        auto fmt = static_cast<texture_format>(format);
        if (fmt == texture_format::unknown) {
            return false;
        }

        return true;
    }
}

void release_mtl_device(void *device_ptr) {
    if (!device_ptr) return;
    @autoreleasepool {
#if __has_feature(objc_arc)
        id<MTLDevice> device = (__bridge_transfer id<MTLDevice>)device_ptr;
        (void)device;
#else
        id<MTLDevice> device = (id<MTLDevice>)device_ptr;
        [device release];
#endif
    }
}

Result<device> wrap_device(const device_desc &desc) {
    if (!desc.device) {
        return Error{ErrorCode::InvalidArgument, "device_desc.device is null"};
    }
    @autoreleasepool {
#if __has_feature(objc_arc)
        id<MTLDevice> mtl_device = (__bridge id<MTLDevice>)desc.device;
        CFRetain((__bridge void *)mtl_device);
        void *ptr = (__bridge_retained void *)mtl_device;
#else
        id<MTLDevice> mtl_device = (id<MTLDevice>)desc.device;
        [mtl_device retain];
        void *ptr = (void *)mtl_device;
#endif
        return detail::make_device_from_backend(ptr);
    }
}

} // namespace bbb::nozzle::metal
