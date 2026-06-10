// nozzle - metal_texture.mm - Metal texture creation and interop

#import <Metal/Metal.h>
#import <IOSurface/IOSurface.h>

#include <nozzle/backends/metal.hpp>
#include <nozzle/sender.hpp>
#include <nozzle/frame.hpp>
#include <nozzle/result.hpp>
#include <nozzle/types.hpp>
#include "metal_helpers.hpp"

#include <cstdint>

namespace nozzle::metal {

static constexpr uint32_t kIOSurfaceAlignBytes = 64;

static MTLPixelFormat to_mtl_pixel_format(uint32_t nozzle_format) {
    switch (static_cast<nozzle::texture_format>(nozzle_format)) {
        case nozzle::texture_format::r8_unorm:         return MTLPixelFormatR8Unorm;
        case nozzle::texture_format::rg8_unorm:        return MTLPixelFormatRG8Unorm;
        case nozzle::texture_format::rgb8_unorm:       return MTLPixelFormatRGBA8Unorm;
        case nozzle::texture_format::rgba8_unorm:      return MTLPixelFormatRGBA8Unorm;
        case nozzle::texture_format::bgra8_unorm:      return MTLPixelFormatBGRA8Unorm;
        case nozzle::texture_format::rgba8_srgb:       return MTLPixelFormatRGBA8Unorm_sRGB;
        case nozzle::texture_format::bgra8_srgb:       return MTLPixelFormatBGRA8Unorm_sRGB;
        case nozzle::texture_format::r16_unorm:        return MTLPixelFormatR16Unorm;
        case nozzle::texture_format::rg16_unorm:       return MTLPixelFormatRG16Unorm;
        case nozzle::texture_format::rgb16_unorm:      return MTLPixelFormatRGBA16Unorm;
        case nozzle::texture_format::rgba16_unorm:     return MTLPixelFormatRGBA16Unorm;
        case nozzle::texture_format::r16_float:        return MTLPixelFormatR16Float;
        case nozzle::texture_format::rg16_float:       return MTLPixelFormatRG16Float;
        case nozzle::texture_format::rgb16_float:      return MTLPixelFormatRGBA16Float;
        case nozzle::texture_format::rgba16_float:     return MTLPixelFormatRGBA16Float;
        case nozzle::texture_format::r32_float:        return MTLPixelFormatR32Float;
        case nozzle::texture_format::rg32_float:       return MTLPixelFormatRG32Float;
        case nozzle::texture_format::rgb32_float:      return MTLPixelFormatRGBA32Float;
        case nozzle::texture_format::rgba32_float:     return MTLPixelFormatRGBA32Float;
        case nozzle::texture_format::r32_uint:         return MTLPixelFormatR32Uint;
        case nozzle::texture_format::rgba32_uint:      return MTLPixelFormatRGBA32Uint;
        case nozzle::texture_format::rgb32_uint:       return MTLPixelFormatRGBA32Uint;
        case nozzle::texture_format::depth32_float:    return MTLPixelFormatDepth32Float;
        default:                                             return MTLPixelFormatInvalid;
    }
}

struct metal_iosurface_format_desc {
    MTLPixelFormat mtl_format{MTLPixelFormatInvalid};
    texture_format storage_format{texture_format::unknown};
    uint32_t iosurface_fourcc{0};
    uint32_t bytes_per_element{0};
    bool ambiguous_iosurface_alias{false};
};

static const metal_iosurface_format_desc *lookup_metal_iosurface_format(MTLPixelFormat mtl_format) {
    static const metal_iosurface_format_desc descs[] = {
        {MTLPixelFormatR8Unorm, texture_format::r8_unorm, 'L008', 1, false},
        {MTLPixelFormatRG8Unorm, texture_format::rg8_unorm, '2C08', 2, false},
        {MTLPixelFormatBGRA8Unorm, texture_format::bgra8_unorm, 'BGRA', 4, true},
        {MTLPixelFormatRGBA8Unorm, texture_format::rgba8_unorm, 'RGBA', 4, true},
        {MTLPixelFormatRGBA8Unorm_sRGB, texture_format::rgba8_srgb, 'RGBA', 4, true},
        {MTLPixelFormatBGRA8Unorm_sRGB, texture_format::bgra8_srgb, 'BGRA', 4, true},
        {MTLPixelFormatR16Unorm, texture_format::r16_unorm, 'L016', 2, false},
        {MTLPixelFormatRG16Unorm, texture_format::rg16_unorm, '2C16', 4, false},
        {MTLPixelFormatRGBA16Unorm, texture_format::rgba16_unorm, 'RGhA', 8, true},
        {MTLPixelFormatR16Float, texture_format::r16_float, 'L00h', 2, false},
        {MTLPixelFormatRG16Float, texture_format::rg16_float, '2C0h', 4, false},
        {MTLPixelFormatRGBA16Float, texture_format::rgba16_float, 'RGhA', 8, true},
        {MTLPixelFormatR32Float, texture_format::r32_float, 'L00f', 4, true},
        {MTLPixelFormatRG32Float, texture_format::rg32_float, '2C0f', 8, false},
        {MTLPixelFormatRGBA32Float, texture_format::rgba32_float, 'RGfA', 16, true},
        {MTLPixelFormatR32Uint, texture_format::r32_uint, 'L00f', 4, true},
        {MTLPixelFormatRGBA32Uint, texture_format::rgba32_uint, 'RGfA', 16, true},
    };

    for (const auto &desc : descs) {
        if (desc.mtl_format == mtl_format) {
            return &desc;
        }
    }
    return nullptr;
}

static bool iosurface_fourcc_is_ambiguous(uint32_t fourcc) {
    for (const MTLPixelFormat mtl_format : {
            MTLPixelFormatR8Unorm,
            MTLPixelFormatRG8Unorm,
            MTLPixelFormatBGRA8Unorm,
            MTLPixelFormatRGBA8Unorm,
            MTLPixelFormatRGBA8Unorm_sRGB,
            MTLPixelFormatBGRA8Unorm_sRGB,
            MTLPixelFormatR16Unorm,
            MTLPixelFormatRG16Unorm,
            MTLPixelFormatRGBA16Unorm,
            MTLPixelFormatR16Float,
            MTLPixelFormatRG16Float,
            MTLPixelFormatRGBA16Float,
            MTLPixelFormatR32Float,
            MTLPixelFormatRG32Float,
            MTLPixelFormatRGBA32Float,
            MTLPixelFormatR32Uint,
            MTLPixelFormatRGBA32Uint}) {
        const auto *desc = lookup_metal_iosurface_format(mtl_format);
        if (desc && desc->iosurface_fourcc == fourcc && desc->ambiguous_iosurface_alias) {
            return true;
        }
    }
    return false;
}

static bool iosurface_layout_accepts_mtl_format(uint32_t fourcc, MTLPixelFormat mtl_format) {
    const auto *desc = lookup_metal_iosurface_format(mtl_format);
    return desc && desc->iosurface_fourcc == fourcc;
}

static bool map_pixel_format(
    MTLPixelFormat mtl_format,
    uint32_t &out_iosurface_pf,
    uint32_t &out_bytes_per_element
) {
    const auto *desc = lookup_metal_iosurface_format(mtl_format);
    if (!desc) {
        return false;
    }
    out_iosurface_pf = desc->iosurface_fourcc;
    out_bytes_per_element = desc->bytes_per_element;
    return true;
}

uint32_t nozzle_format_to_mtl(uint32_t nozzle_format) {
    return static_cast<uint32_t>(to_mtl_pixel_format(nozzle_format));
}

bool mtl_format_to_iosurface(uint32_t mtl_format, uint32_t &out_pf, uint32_t &out_bpe) {
    return map_pixel_format(static_cast<MTLPixelFormat>(mtl_format), out_pf, out_bpe);
}

bool mtl_format_to_storage_format(uint32_t mtl_format, uint32_t &out_storage_format) {
    const auto *desc = lookup_metal_iosurface_format(static_cast<MTLPixelFormat>(mtl_format));
    if (!desc) {
        return false;
    }
    out_storage_format = static_cast<uint32_t>(desc->storage_format);
    return true;
}

bool iosurface_format_accepts_mtl(uint32_t iosurface_fourcc, uint32_t mtl_format) {
    return iosurface_layout_accepts_mtl_format(
        iosurface_fourcc,
        static_cast<MTLPixelFormat>(mtl_format));
}

bool iosurface_format_requires_native_mtl(uint32_t iosurface_fourcc) {
    return iosurface_fourcc_is_ambiguous(iosurface_fourcc);
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

        auto nozzle_fmt = static_cast<texture_format>(pixel_format);
        if (nozzle_fmt == texture_format::rgba8_srgb || nozzle_fmt == texture_format::bgra8_srgb) {
            return Error{
                ErrorCode::UnsupportedFormat,
                "sRGB formats are not supported for IOSurface-backed textures in v0.1"
            };
        }
        if (nozzle_fmt == texture_format::rgba8_unorm) {
            nozzle_fmt = texture_format::bgra8_unorm;
        }

        auto mtl_format = to_mtl_pixel_format(static_cast<uint32_t>(nozzle_fmt));
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

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
        NSDictionary *surface_props = @{
            (id)kIOSurfaceIsGlobal:     @(YES),
            (id)kIOSurfaceWidth:        @(width),
            (id)kIOSurfaceHeight:       @(height),
            (id)kIOSurfacePixelFormat:  @(iosurface_pf),
            (id)kIOSurfaceBytesPerRow:  @(bytes_per_row),
            (id)kIOSurfaceBytesPerElement: @(bytes_per_element),
        };
#pragma clang diagnostic pop

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
        // Ring textures are filled through IOSurface CPU mappings or Metal blits,
        // then read by receivers. Requiring ShaderWrite here is unnecessarily
        // restrictive for IOSurface-backed BGRA/RGBA textures on some macOS
        // devices/runners and can make newTextureWithDescriptor:iosurface:
        // return nil before any frame is published.
        tex_desc.usage = MTLTextureUsageShaderRead;
        tex_desc.storageMode = MTLStorageModeShared;

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

        OSType actual_fourcc = IOSurfaceGetPixelFormat(surface);
        const auto *mtl_desc = lookup_metal_iosurface_format(mtl_format);
        result.pixel_format = static_cast<uint32_t>(
            mtl_desc ? mtl_desc->storage_format : nozzle_fmt);
        result.fourcc = static_cast<uint32_t>(actual_fourcc);
        result.mtl_pixel_format = static_cast<uint32_t>(mtl_format);

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

        void *surface_ptr = nullptr;
        uint32_t actual_fmt = desc.format;
        native_format_desc native{};
        native.backend = backend_type::metal;
        native.kind = native_format_kind::mtl_pixel_format;
        native.value = static_cast<uint32_t>(mtl_tex.pixelFormat);
        const auto *mtl_desc = lookup_metal_iosurface_format(mtl_tex.pixelFormat);
        if (mtl_desc) {
            if (desc.format != static_cast<uint32_t>(mtl_desc->storage_format)) {
                return Error{ErrorCode::UnsupportedFormat,
                    "wrapped Metal texture format does not match MTLPixelFormat"};
            }
            actual_fmt = static_cast<uint32_t>(mtl_desc->storage_format);
        }
        if (desc.io_surface) {
            IOSurfaceRef surface = (IOSurfaceRef)desc.io_surface;
            CFRetain(surface);
            surface_ptr = (void *)surface;
            if (mtl_desc &&
                !iosurface_layout_accepts_mtl_format(
                    static_cast<uint32_t>(IOSurfaceGetPixelFormat(surface)),
                    mtl_tex.pixelFormat)) {
                CFRelease(surface);
                return Error{ErrorCode::UnsupportedFormat,
                    "wrapped IOSurface pixel format is not compatible with MTLPixelFormat"};
            }
        }

        NOZZLE_RETAIN(mtl_tex);
        void *tex_ptr = NOZZLE_BRIDGE_RETAIN(id<MTLTexture>, mtl_tex);

        return detail::make_texture_from_backend(
            tex_ptr,
            surface_ptr,
            desc.width,
            desc.height,
            actual_fmt,
            static_cast<uint8_t>(desc.swizzle),
            &native,
            static_cast<uint32_t>(desc.format)
        );
    }
}

mtl_texture_handle get_texture(const texture &tex) {
    return detail::get_texture_native(tex);
}

surface_handle get_io_surface(const texture &tex) {
    return detail::get_surface_native(tex);
}

} // namespace nozzle::metal

// lookup_iosurface_texture: create a Metal texture from an existing IOSurface ID
// (used by receiver to access sender's textures cross-process)
namespace nozzle::metal {

Result<texture> lookup_iosurface_texture(
    uint32_t iosurface_id,
    uint32_t width,
    uint32_t height,
    uint32_t pixel_format,
    uint8_t channel_swizzle_val,
    uint32_t semantic_format,
    uint8_t native_format_kind_val,
    uint32_t native_format_value
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

        OSType surface_fourcc = IOSurfaceGetPixelFormat(surface);
        texture_format nozzle_fmt = static_cast<texture_format>(pixel_format);
        bool native_mtl_metadata = false;

        auto mtl_format = MTLPixelFormatInvalid;
        if (native_format_kind_val == static_cast<uint8_t>(native_format_kind::mtl_pixel_format) &&
            native_format_value != 0) {
            mtl_format = static_cast<MTLPixelFormat>(native_format_value);
            native_mtl_metadata = true;
        } else {
            if (iosurface_fourcc_is_ambiguous(static_cast<uint32_t>(surface_fourcc))) {
                CFRelease(surface);
                return Error{
                    ErrorCode::UnsupportedFormat,
                    "Ambiguous IOSurface pixel format requires native Metal format metadata"
                };
            }
            if (nozzle_fmt == texture_format::unknown) {
                nozzle_fmt = from_io_surface_pixel_format(surface_fourcc);
            }
            if (nozzle_fmt == texture_format::unknown) {
                CFRelease(surface);
                return Error{ErrorCode::UnsupportedFormat, "Unknown IOSurface pixel format"};
            }
            mtl_format = to_mtl_pixel_format(static_cast<uint32_t>(nozzle_fmt));
        }
        if (mtl_format == MTLPixelFormatInvalid) {
            CFRelease(surface);
            return Error{ErrorCode::UnsupportedFormat, "Unsupported nozzle pixel format"};
        }
        const auto *mtl_desc = lookup_metal_iosurface_format(mtl_format);
        if (!mtl_desc) {
            CFRelease(surface);
            return Error{ErrorCode::UnsupportedFormat, "Unsupported Metal pixel format"};
        }
        if (!iosurface_layout_accepts_mtl_format(static_cast<uint32_t>(surface_fourcc), mtl_format)) {
            CFRelease(surface);
            return Error{ErrorCode::UnsupportedFormat, "IOSurface pixel format is not compatible with native Metal format"};
        }
        if (native_mtl_metadata) {
            if (nozzle_fmt == texture_format::unknown) {
                nozzle_fmt = mtl_desc->storage_format;
            } else if (nozzle_fmt != mtl_desc->storage_format) {
                CFRelease(surface);
                return Error{ErrorCode::UnsupportedFormat, "Published storage format does not match native Metal format"};
            }
        }

        MTLTextureDescriptor *tex_desc =
            [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:mtl_format
                                                               width:width
                                                              height:height
                                                           mipmapped:NO];
        tex_desc.usage = MTLTextureUsageShaderRead;
        tex_desc.storageMode = MTLStorageModeShared;

        id<MTLTexture> base_texture = [device newTextureWithDescriptor:tex_desc
                                                              iosurface:surface
                                                                    plane:0];
        if (!base_texture) {
            CFRelease(surface);
            return Error{
                ErrorCode::ResourceCreationFailed,
                "Failed to create Metal texture from looked-up IOSurface"
            };
        }

        id<MTLTexture> final_texture = base_texture;

        if (static_cast<nozzle::channel_swizzle>(channel_swizzle_val) == nozzle::channel_swizzle::swap_rb) {
            MTLTextureSwizzleChannels swizzle = {
                .red   = MTLTextureSwizzleBlue,
                .green = MTLTextureSwizzleGreen,
                .blue  = MTLTextureSwizzleRed,
                .alpha = MTLTextureSwizzleAlpha
            };
            final_texture = [base_texture newTextureViewWithPixelFormat:mtl_format
                                                            textureType:base_texture.textureType
                                                                 levels:NSMakeRange(0, base_texture.mipmapLevelCount)
                                                                 slices:NSMakeRange(0, base_texture.arrayLength)
                                                                swizzle:swizzle];
            if (!final_texture) {
#if !__has_feature(objc_arc)
                [base_texture release];
#endif
                CFRelease(surface);
                return Error{
                    ErrorCode::ResourceCreationFailed,
                    "Failed to create swizzled texture view"
                };
            }
#if !__has_feature(objc_arc)
            [base_texture release];
#endif
        }

        native_format_desc native{};
        native.backend = backend_type::metal;
        native.kind = native_format_kind::mtl_pixel_format;
        native.value = static_cast<uint32_t>(mtl_format);

        return detail::make_texture_from_backend(
            NOZZLE_BRIDGE_RETAIN(id<MTLTexture>, final_texture),
            (void *)surface,
            width,
            height,
            static_cast<uint32_t>(nozzle_fmt),
            0,
            &native,
            semantic_format
        );
    }
}

Result<texture> wrap_direct_publish_texture(
    void *mtl_device_ptr,
    const direct_publish_desc &desc
) {
    if (!desc.texture) {
        return Error{ErrorCode::InvalidArgument, "direct Metal texture is null"};
    }
    if (desc.width == 0 || desc.height == 0) {
        return Error{ErrorCode::InvalidArgument, "direct Metal texture dimensions must be non-zero"};
    }
    if (desc.storage_format == texture_format::unknown ||
        desc.semantic_format == texture_format::unknown) {
        return Error{ErrorCode::UnsupportedFormat,
            "direct Metal publish requires explicit storage and semantic formats"};
    }
    if (desc.semantic_format != desc.storage_format) {
        return Error{ErrorCode::UnsupportedFormat,
            "direct Metal publish requires semantic format to match storage format"};
    }

    @autoreleasepool {
        id<MTLDevice> device = NOZZLE_BRIDGE_GET(id<MTLDevice>, mtl_device_ptr);
        id<MTLTexture> mtl_texture = NOZZLE_BRIDGE_GET(id<MTLTexture>, desc.texture);
        if (!device || !mtl_texture) {
            return Error{ErrorCode::InvalidArgument, "direct Metal publish: null device or texture"};
        }
        if (mtl_texture.device != device) {
            return Error{ErrorCode::DeviceMismatch,
                "direct Metal texture device does not match sender device"};
        }
        if (mtl_texture.width != desc.width || mtl_texture.height != desc.height) {
            return Error{ErrorCode::InvalidArgument,
                "direct Metal texture dimensions do not match descriptor"};
        }

        IOSurfaceRef surface = mtl_texture.iosurface;
        if (!surface) {
            return Error{ErrorCode::InvalidArgument,
                "direct Metal publish requires an IOSurface-backed texture"};
        }

        IOSurfaceID surface_id = IOSurfaceGetID(surface);
        if (surface_id == 0) {
            return Error{ErrorCode::InvalidArgument,
                "direct Metal publish requires a globally lookupable IOSurface"};
        }
        IOSurfaceRef lookup_surface = IOSurfaceLookup(surface_id);
        if (!lookup_surface) {
            return Error{ErrorCode::InvalidArgument,
                "direct Metal publish requires a globally lookupable IOSurface"};
        }
        CFRelease(lookup_surface);

        const auto *mtl_desc = lookup_metal_iosurface_format(mtl_texture.pixelFormat);
        if (!mtl_desc) {
            return Error{ErrorCode::UnsupportedFormat,
                "direct Metal publish requires a supported MTLPixelFormat"};
        }

        if (mtl_desc->storage_format != desc.storage_format) {
            return Error{ErrorCode::UnsupportedFormat,
                "direct Metal publish storage format does not match MTLPixelFormat"};
        }

        MTLPixelFormat expected_mtl_format = to_mtl_pixel_format(static_cast<uint32_t>(desc.storage_format));
        if (expected_mtl_format == MTLPixelFormatInvalid) {
            return Error{ErrorCode::UnsupportedFormat,
                "direct Metal publish storage format has no supported MTLPixelFormat"};
        }
        if (mtl_texture.pixelFormat != expected_mtl_format) {
            return Error{ErrorCode::UnsupportedFormat,
                "direct Metal publish storage format does not match MTLPixelFormat"};
        }

        OSType surface_fourcc = IOSurfaceGetPixelFormat(surface);
        if (!iosurface_layout_accepts_mtl_format(static_cast<uint32_t>(surface_fourcc), mtl_texture.pixelFormat)) {
            return Error{ErrorCode::UnsupportedFormat,
                "direct Metal publish IOSurface pixel format is not compatible with MTLPixelFormat"};
        }

#if __has_feature(objc_arc)
        void *texture_ptr = NOZZLE_BRIDGE_RETAIN(id<MTLTexture>, mtl_texture);
#else
        NOZZLE_RETAIN(mtl_texture);
        void *texture_ptr = (void *)mtl_texture;
#endif
        CFRetain(surface);

        native_format_desc native{};
        native.backend = backend_type::metal;
        native.kind = native_format_kind::mtl_pixel_format;
        native.value = static_cast<uint32_t>(mtl_texture.pixelFormat);

        return detail::make_texture_from_backend(
            texture_ptr,
            (void *)surface,
            desc.width,
            desc.height,
            static_cast<uint32_t>(desc.storage_format),
            static_cast<uint8_t>(desc.swizzle),
            &native,
            static_cast<uint32_t>(desc.semantic_format)
        );
    }
}

bool is_iosurface_backed(void *mtl_texture_ptr) {
    @autoreleasepool {
        id<MTLTexture> tex = NOZZLE_BRIDGE_GET(id<MTLTexture>, mtl_texture_ptr);
        if (!tex) return false;
        return tex.iosurface != nil;
    }
}

void *get_io_surface_from_texture(void *mtl_texture_ptr) {
    @autoreleasepool {
        id<MTLTexture> tex = NOZZLE_BRIDGE_GET(id<MTLTexture>, mtl_texture_ptr);
        if (!tex) return nullptr;
        IOSurfaceRef surface = tex.iosurface;
        if (surface) {
            CFRetain(surface);
            return (void *)surface;
        }
        return nullptr;
    }
}

Result<void> check_texture_device(void *mtl_device_ptr, void *mtl_texture_ptr) {
    @autoreleasepool {
        id<MTLDevice> device = NOZZLE_BRIDGE_GET(id<MTLDevice>, mtl_device_ptr);
        id<MTLTexture> tex = NOZZLE_BRIDGE_GET(id<MTLTexture>, mtl_texture_ptr);
        if (!device || !tex) {
            return Error{ErrorCode::InvalidArgument, "check_texture_device: null argument"};
        }
        if (tex.device != device) {
            return Error{ErrorCode::DeviceMismatch,
                "source texture device does not match sender device"};
        }
        return {};
    }
}

Result<void> blit_to_texture(void *mtl_device_ptr, void *src_ptr, void *dst_ptr, uint32_t width, uint32_t height) {
    @autoreleasepool {
        id<MTLDevice> device = NOZZLE_BRIDGE_GET(id<MTLDevice>, mtl_device_ptr);
        id<MTLTexture> src = NOZZLE_BRIDGE_GET(id<MTLTexture>, src_ptr);
        id<MTLTexture> dst = NOZZLE_BRIDGE_GET(id<MTLTexture>, dst_ptr);

        if (!device || !src || !dst) {
            return Error{ErrorCode::InvalidArgument, "blit_to_texture: null argument"};
        }

        id<MTLCommandQueue> queue = device.newCommandQueue;
        if (!queue) {
            return Error{ErrorCode::BackendError, "failed to create command queue for blit"};
        }

        id<MTLCommandBuffer> cmd = queue.commandBuffer;
        if (!cmd) {
#if !__has_feature(objc_arc)
            [queue release];
#endif
            return Error{ErrorCode::CommandFailed, "blit_to_texture: failed to create command buffer"};
        }

        id<MTLBlitCommandEncoder> blit = cmd.blitCommandEncoder;
        if (!blit) {
#if !__has_feature(objc_arc)
            [queue release];
#endif
            return Error{ErrorCode::CommandFailed, "blit_to_texture: failed to create blit command encoder"};
        }

        [blit copyFromTexture:src
                 sourceSlice:0
                 sourceLevel:0
                sourceOrigin:MTLOriginMake(0, 0, 0)
                  sourceSize:MTLSizeMake(width, height, 1)
                   toTexture:dst
            destinationSlice:0
            destinationLevel:0
           destinationOrigin:MTLOriginMake(0, 0, 0)];

        [blit endEncoding];
        [cmd commit];
        [cmd waitUntilCompleted];

        if (cmd.status != MTLCommandBufferStatusCompleted) {
            std::string msg = "blit_to_texture: command buffer execution failed";
            if (cmd.error) {
                const char *desc = cmd.error.localizedDescription.UTF8String;
                if (desc) {
                    msg += ": ";
                    msg += desc;
                }
            }
#if !__has_feature(objc_arc)
            [queue release];
#endif
            return Error{ErrorCode::CommandFailed, std::move(msg)};
        }

#if !__has_feature(objc_arc)
        [queue release];
#endif

        return {};
    }
}

Result<void> blit_from_texture(void *mtl_device_ptr, void *src_ptr, void *dst_ptr, uint32_t width, uint32_t height) {
    return blit_to_texture(mtl_device_ptr, src_ptr, dst_ptr, width, height);
}

surface_handle get_io_surface_from_native_texture(mtl_texture_handle native_texture_ptr) {
    return get_io_surface_from_texture(native_texture_ptr);
}

bool is_native_texture_iosurface_backed(mtl_texture_handle native_texture_ptr) {
    return is_iosurface_backed(native_texture_ptr);
}

texture_format from_io_surface_pixel_format(uint32_t ostype) {
    switch (ostype) {
        case 'L008': return texture_format::r8_unorm;
        case '2C08': return texture_format::rg8_unorm;
        case 'BGRA': return texture_format::bgra8_unorm;
        case 'RGBA': return texture_format::rgba8_unorm;
        case 'L016': return texture_format::r16_unorm;
        case '2C16': return texture_format::rg16_unorm;
        case 'L00h': return texture_format::r16_float;
        case '2C0h': return texture_format::rg16_float;
        case 'RGhA': return texture_format::rgba16_float;
        case 'L00f': return texture_format::r32_float;
        case '2C0f': return texture_format::rg32_float;
        case 'RGfA': return texture_format::rgba32_float;
        default:     return texture_format::unknown;
    }
}

} // namespace nozzle::metal
