// nozzle - metal_texture.mm - Metal texture creation and interop

#import <Metal/Metal.h>
#import <IOSurface/IOSurface.h>

#include <bbb/nozzle/backends/metal.hpp>
#include <bbb/nozzle/result.hpp>
#include <bbb/nozzle/types.hpp>
#include "metal_helpers.hpp"

#include <cstdint>

namespace bbb::nozzle::metal {

static constexpr uint32_t kIOSurfaceAlignBytes = 64;

static MTLPixelFormat to_mtl_pixel_format(uint32_t nozzle_format) {
    switch (static_cast<bbb::nozzle::texture_format>(nozzle_format)) {
        case bbb::nozzle::texture_format::r8_unorm:         return MTLPixelFormatR8Unorm;
        case bbb::nozzle::texture_format::rg8_unorm:        return MTLPixelFormatRG8Unorm;
        case bbb::nozzle::texture_format::rgba8_unorm:      return MTLPixelFormatRGBA8Unorm;
        case bbb::nozzle::texture_format::bgra8_unorm:      return MTLPixelFormatBGRA8Unorm;
        case bbb::nozzle::texture_format::rgba8_srgb:       return MTLPixelFormatRGBA8Unorm_sRGB;
        case bbb::nozzle::texture_format::bgra8_srgb:       return MTLPixelFormatBGRA8Unorm_sRGB;
        case bbb::nozzle::texture_format::r16_unorm:        return MTLPixelFormatR16Unorm;
        case bbb::nozzle::texture_format::rg16_unorm:       return MTLPixelFormatRG16Unorm;
        case bbb::nozzle::texture_format::rgba16_unorm:     return MTLPixelFormatRGBA16Unorm;
        case bbb::nozzle::texture_format::r16_float:        return MTLPixelFormatR16Float;
        case bbb::nozzle::texture_format::rg16_float:       return MTLPixelFormatRG16Float;
        case bbb::nozzle::texture_format::rgba16_float:     return MTLPixelFormatRGBA16Float;
        case bbb::nozzle::texture_format::r32_float:        return MTLPixelFormatR32Float;
        case bbb::nozzle::texture_format::rg32_float:       return MTLPixelFormatRG32Float;
        case bbb::nozzle::texture_format::rgba32_float:     return MTLPixelFormatRGBA32Float;
        case bbb::nozzle::texture_format::r32_uint:         return MTLPixelFormatR32Uint;
        case bbb::nozzle::texture_format::rgba32_uint:      return MTLPixelFormatRGBA32Uint;
        case bbb::nozzle::texture_format::depth32_float:    return MTLPixelFormatDepth32Float;
        default:                                             return MTLPixelFormatInvalid;
    }
}

static bool map_pixel_format(
    MTLPixelFormat mtl_format,
    uint32_t &out_iosurface_pf,
    uint32_t &out_bytes_per_element
) {
    switch (mtl_format) {
        case MTLPixelFormatR8Unorm:
            out_iosurface_pf = 'L008'; // kCVPixelFormatType_OneComponent8
            out_bytes_per_element = 1;
            return true;
        case MTLPixelFormatRG8Unorm:
            out_iosurface_pf = '2C08'; // kCVPixelFormatType_TwoComponent8
            out_bytes_per_element = 2;
            return true;
        case MTLPixelFormatBGRA8Unorm:
            out_iosurface_pf = 'BGRA'; // kCVPixelFormatType_32BGRA
            out_bytes_per_element = 4;
            return true;
        case MTLPixelFormatRGBA8Unorm:
            out_iosurface_pf = 'RGBA'; // kCVPixelFormatType_32RGBA
            out_bytes_per_element = 4;
            return true;
        case MTLPixelFormatRGBA8Unorm_sRGB:
            out_iosurface_pf = 'RGBA'; // kCVPixelFormatType_32RGBA (sRGB handled by Metal)
            out_bytes_per_element = 4;
            return true;
        case MTLPixelFormatBGRA8Unorm_sRGB:
            out_iosurface_pf = 'BGRA'; // kCVPixelFormatType_32BGRA (sRGB handled by Metal)
            out_bytes_per_element = 4;
            return true;
        case MTLPixelFormatR16Unorm:
            out_iosurface_pf = 'L016'; // kCVPixelFormatType_OneComponent16
            out_bytes_per_element = 2;
            return true;
        case MTLPixelFormatRG16Unorm:
            out_iosurface_pf = '2C16'; // kCVPixelFormatType_TwoComponent16
            out_bytes_per_element = 4;
            return true;
        case MTLPixelFormatR16Float:
            out_iosurface_pf = 'L00h'; // kCVPixelFormatType_OneComponent16Half
            out_bytes_per_element = 2;
            return true;
        case MTLPixelFormatRG16Float:
            out_iosurface_pf = '2C0h'; // kCVPixelFormatType_TwoComponent16Half
            out_bytes_per_element = 4;
            return true;
        case MTLPixelFormatRGBA16Float:
            out_iosurface_pf = 'RGhA'; // kCVPixelFormatType_64RGBAHalf
            out_bytes_per_element = 8;
            return true;
        case MTLPixelFormatR32Float:
            out_iosurface_pf = 'L00f'; // kCVPixelFormatType_OneComponent32Float
            out_bytes_per_element = 4;
            return true;
        case MTLPixelFormatRG32Float:
            out_iosurface_pf = '2C0f'; // kCVPixelFormatType_TwoComponent32Float
            out_bytes_per_element = 8;
            return true;
        case MTLPixelFormatRGBA32Float:
            out_iosurface_pf = 'RGfA'; // kCVPixelFormatType_128RGBAFloat
            out_bytes_per_element = 16;
            return true;
        default:
            return false;
    }
}

uint32_t nozzle_format_to_mtl(uint32_t nozzle_format) {
    return static_cast<uint32_t>(to_mtl_pixel_format(nozzle_format));
}

bool mtl_format_to_iosurface(uint32_t mtl_format, uint32_t &out_pf, uint32_t &out_bpe) {
    return map_pixel_format(static_cast<MTLPixelFormat>(mtl_format), out_pf, out_bpe);
}

static uint32_t align_up(uint32_t value, uint32_t alignment) {
    return (value + alignment - 1) & ~(alignment - 1);
}

// Helper: convert void* to ObjC object with appropriate cast
#if __has_feature(objc_arc)
    #define NOZZLE_BRIDGE_GET(T, ptr)       ((__bridge T)(ptr))
    #define NOZZLE_BRIDGE_RETAIN(T, obj)    (__bridge_retained void *)(obj)
    #define NOZZLE_BRIDGE_RELEASE(ptr)      do { \
        __unused id _tmp = (__bridge_transfer id)(ptr); \
    } while(0)
    #define NOZZLE_RETAIN(obj)              CFRetain((__bridge void *)(obj))
#else
    #define NOZZLE_BRIDGE_GET(T, ptr)       ((T)(ptr))
    #define NOZZLE_BRIDGE_RETAIN(T, obj)    ((void *)(obj))
    #define NOZZLE_BRIDGE_RELEASE(ptr)      do { \
        [(id)(ptr) release]; \
    } while(0)
    #define NOZZLE_RETAIN(obj)              [(obj) retain]
#endif

Result<metal_texture_pair> create_iosurface_texture(
    void *mtl_device_ptr,
    uint32_t width,
    uint32_t height,
    uint32_t pixel_format
) {
    @autoreleasepool {
        id<MTLDevice> mtl_device = NOZZLE_BRIDGE_GET(id<MTLDevice>, mtl_device_ptr);
        if (!mtl_device) {
            return Error{ErrorCode::InvalidArgument, "Metal device is null"};
        }
        if (width == 0 || height == 0) {
            return Error{ErrorCode::InvalidArgument, "Texture dimensions must be non-zero"};
        }

        auto mtl_format = to_mtl_pixel_format(pixel_format);
        if (mtl_format == MTLPixelFormatInvalid) {
            return Error{
                ErrorCode::UnsupportedFormat,
                "Unsupported nozzle pixel format for IOSurface-backed texture"
            };
        }

        uint32_t iosurface_pf = 0;
        uint32_t bytes_per_element = 0;
        if (!map_pixel_format(mtl_format, iosurface_pf, bytes_per_element)) {
            return Error{
                ErrorCode::UnsupportedFormat,
                "Unsupported pixel format for IOSurface-backed texture"
            };
        }

        uint32_t bytes_per_row = align_up(width * bytes_per_element, kIOSurfaceAlignBytes);

        NSDictionary *surface_props = @{
            (id)kIOSurfaceWidth:        @(width),
            (id)kIOSurfaceHeight:       @(height),
            (id)kIOSurfacePixelFormat:  @(iosurface_pf),
            (id)kIOSurfaceBytesPerRow:  @(bytes_per_row),
            (id)kIOSurfaceBytesPerElement: @(bytes_per_element),
        };

        IOSurfaceRef surface = IOSurfaceCreate((CFDictionaryRef)surface_props);
        if (!surface) {
            return Error{
                ErrorCode::ResourceCreationFailed,
                "Failed to create IOSurface"
            };
        }

        MTLTextureDescriptor *tex_desc =
            [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:mtl_format
                                                               width:width
                                                              height:height
                                                           mipmapped:NO];
        tex_desc.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;
        tex_desc.resourceOptions = MTLResourceStorageModeShared;

        id<MTLTexture> texture = [mtl_device newTextureWithDescriptor:tex_desc
                                                            iosurface:surface
                                                                  plane:0];
        if (!texture) {
            CFRelease(surface);
            return Error{
                ErrorCode::ResourceCreationFailed,
                "Failed to create Metal texture from IOSurface"
            };
        }

        metal_texture_pair result{};
        result.mtl_texture = NOZZLE_BRIDGE_RETAIN(id<MTLTexture>, texture);
        result.io_surface = (void *)surface;
        result.io_surface_id = IOSurfaceGetID(surface);
        result.width = width;
        result.height = height;
        result.pixel_format = pixel_format;

        return result;
    }
}

uint32_t get_iosurface_id(void *io_surface_ptr) {
    @autoreleasepool {
        IOSurfaceRef surface = (IOSurfaceRef)io_surface_ptr;
        if (!surface) {
            return 0;
        }
        return IOSurfaceGetID(surface);
    }
}

void release_mtl_texture_resources(void *texture_ptr, void *surface_ptr) {
    if (texture_ptr) {
        NOZZLE_BRIDGE_RELEASE(texture_ptr);
    }
    if (surface_ptr) {
        CFRelease((IOSurfaceRef)surface_ptr);
    }
}

Result<texture> wrap_texture(const texture_wrap_desc &desc) {
    if (!desc.texture) {
        return Error{ErrorCode::InvalidArgument, "texture_wrap_desc.texture is null"};
    }
    @autoreleasepool {
        id<MTLTexture> mtl_tex = NOZZLE_BRIDGE_GET(id<MTLTexture>, desc.texture);
        NOZZLE_RETAIN(mtl_tex);
        void *tex_ptr = NOZZLE_BRIDGE_RETAIN(id<MTLTexture>, mtl_tex);

        void *surface_ptr = nullptr;
        if (desc.io_surface) {
            IOSurfaceRef surface = (IOSurfaceRef)desc.io_surface;
            CFRetain(surface);
            surface_ptr = (void *)surface;
        }

        return detail::make_texture_from_backend(
            tex_ptr,
            surface_ptr,
            desc.width,
            desc.height,
            desc.format
        );
    }
}

mtl_texture_handle get_texture(const texture &tex) {
    return detail::get_texture_native(tex);
}

surface_handle get_io_surface(const texture &tex) {
    return detail::get_surface_native(tex);
}

} // namespace bbb::nozzle::metal

// lookup_iosurface_texture: create a Metal texture from an existing IOSurface ID
// (used by receiver to access sender's textures cross-process)
namespace bbb::nozzle::metal {

Result<texture> lookup_iosurface_texture(
    uint32_t iosurface_id,
    uint32_t width,
    uint32_t height,
    uint32_t pixel_format
) {
    @autoreleasepool {
        IOSurfaceRef surface = IOSurfaceLookup(iosurface_id);
        if (!surface) {
            return Error{
                ErrorCode::ResourceCreationFailed,
                "Failed to lookup IOSurface by ID"
            };
        }

        id<MTLDevice> device = NOZZLE_BRIDGE_GET(id<MTLDevice>, get_default_mtl_device());
        if (!device) {
            CFRelease(surface);
            return Error{ErrorCode::BackendError, "No default Metal device"};
        }

        auto mtl_format = to_mtl_pixel_format(pixel_format);
        if (mtl_format == MTLPixelFormatInvalid) {
            CFRelease(surface);
            return Error{ErrorCode::UnsupportedFormat, "Unsupported nozzle pixel format"};
        }

        MTLTextureDescriptor *tex_desc =
            [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:mtl_format
                                                               width:width
                                                              height:height
                                                           mipmapped:NO];
        tex_desc.usage = MTLTextureUsageShaderRead;
        tex_desc.resourceOptions = MTLResourceStorageModeShared;

        id<MTLTexture> texture = [device newTextureWithDescriptor:tex_desc
                                                         iosurface:surface
                                                               plane:0];
        if (!texture) {
            CFRelease(surface);
            return Error{
                ErrorCode::ResourceCreationFailed,
                "Failed to create Metal texture from looked-up IOSurface"
            };
        }

        return detail::make_texture_from_backend(
            NOZZLE_BRIDGE_RETAIN(id<MTLTexture>, texture),
            (void *)surface,
            width,
            height,
            pixel_format
        );
    }
}

} // namespace bbb::nozzle::metal
