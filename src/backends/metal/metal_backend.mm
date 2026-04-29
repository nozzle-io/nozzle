// nozzle - metal_backend.mm - macOS Metal backend (IOSurface-backed shared textures)

#import <Metal/Metal.h>
#import <IOSurface/IOSurface.h>

#include <bbb/nozzle/backends/metal.hpp>
#include <bbb/nozzle/result.hpp>
#include <bbb/nozzle/types.hpp>
#include "metal_helpers.hpp"

#include <mutex>
#include <unordered_map>

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

namespace {

struct format_probe_cache {
    std::unordered_map<uint32_t, bool> cache;
    std::mutex mutex;

    static format_probe_cache &instance() {
        static format_probe_cache inst;
        return inst;
    }
};

} // anonymous namespace

bool metal_supports_format(void *device_ptr, uint32_t format, uint32_t usage) {
    (void)usage;
    auto fmt = static_cast<texture_format>(format);
    if (fmt == texture_format::unknown) {
        return false;
    }

    auto &cache = format_probe_cache::instance();
    {
        std::lock_guard<std::mutex> lock(cache.mutex);
        auto it = cache.cache.find(format);
        if (it != cache.cache.end()) {
            return it->second;
        }
    }

    @autoreleasepool {
#if __has_feature(objc_arc)
        id<MTLDevice> device = (__bridge id<MTLDevice>)device_ptr;
#else
        id<MTLDevice> device = (id<MTLDevice>)device_ptr;
#endif
        if (!device) {
            return false;
        }

        // Convert nozzle format to MTLPixelFormat and IOSurface format
        uint32_t mtl_fmt_u32 = nozzle_format_to_mtl(format);
        MTLPixelFormat mtl_fmt = static_cast<MTLPixelFormat>(mtl_fmt_u32);
        if (mtl_fmt == MTLPixelFormatInvalid) {
            std::lock_guard<std::mutex> lock(cache.mutex);
            cache.cache[format] = false;
            return false;
        }

        uint32_t iosurface_pf = 0;
        uint32_t bpe = 0;
        if (!mtl_format_to_iosurface(mtl_fmt_u32, iosurface_pf, bpe)) {
            std::lock_guard<std::mutex> lock(cache.mutex);
            cache.cache[format] = false;
            return false;
        }

        // Probe: create 1x1 IOSurface
        NSDictionary *props = @{
            (id)kIOSurfaceWidth: @1,
            (id)kIOSurfaceHeight: @1,
            (id)kIOSurfaceBytesPerElement: @(bpe),
            (id)kIOSurfacePixelFormat: @(iosurface_pf),
        };
        IOSurfaceRef surface = IOSurfaceCreate((CFDictionaryRef)props);
        if (!surface) {
            std::lock_guard<std::mutex> lock(cache.mutex);
            cache.cache[format] = false;
            return false;
        }

        // Probe: create Metal texture from IOSurface
        MTLTextureDescriptor *desc =
            [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:mtl_fmt
                                                               width:1
                                                              height:1
                                                           mipmapped:NO];
        desc.storageMode = MTLStorageModeShared;

        id<MTLTexture> texture = [device newTextureWithDescriptor:desc
                                                         iosurface:surface
                                                             plane:0];
        bool supported = (texture != nil);

        CFRelease(surface);

        std::lock_guard<std::mutex> lock(cache.mutex);
        cache.cache[format] = supported;
        return supported;
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
