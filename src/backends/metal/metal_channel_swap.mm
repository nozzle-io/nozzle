// nozzle - metal_channel_swap.mm - Metal compute shader for R↔B channel swap on IOSurface textures

#import <Metal/Metal.h>

#include <nozzle/types.hpp>
#include <nozzle/result.hpp>
#include "metal_helpers.hpp"

#include <cstdint>

namespace nozzle::metal {

#if __has_feature(objc_arc)
    #define NOZZLE_BRIDGE_GET(T, ptr) ((__bridge T)(ptr))
#else
    #define NOZZLE_BRIDGE_GET(T, ptr) ((T)(ptr))
#endif

static NSString *const kSwapRBKernel = @""
    "#include <metal_stdlib>\n"
    "using namespace metal;\n"
    "kernel void swap_rb(\n"
    "    texture2d<float, access::read> src [[texture(0)]],\n"
    "    texture2d<float, access::write> dst [[texture(1)]],\n"
    "    uint2 gid [[thread_position_in_grid]]\n"
    ") {\n"
    "    if (gid.x >= src.get_width() || gid.y >= src.get_height()) return;\n"
    "    float4 c = src.read(gid);\n"
    "    dst.write(float4(c.b, c.g, c.r, c.a), gid);\n"
    "}\n";

static id<MTLComputePipelineState> s_pipeline = nil;
static id<MTLCommandQueue> s_queue = nil;
static dispatch_once_t s_init_once;

static bool ensure_pipeline(id<MTLDevice> device) {
    dispatch_once(&s_init_once, ^{
        @autoreleasepool {
            NSError *err = nil;
            id<MTLLibrary> lib = [device newLibraryWithSource:kSwapRBKernel options:nil error:&err];
            if (!lib) return;

            id<MTLFunction> fn = [lib newFunctionWithName:@"swap_rb"];
            if (!fn) return;

            s_pipeline = [device newComputePipelineStateWithFunction:fn error:&err];
            if (!s_pipeline) return;

            s_queue = [device newCommandQueue];
        }
    });
    return s_pipeline && s_queue;
}

Result<void> swap_rb_channels(void *mtl_texture_ptr, uint32_t width, uint32_t height) {
    @autoreleasepool {
        id<MTLTexture> src = NOZZLE_BRIDGE_GET(id<MTLTexture>, mtl_texture_ptr);
        if (!src) {
            return Error{ErrorCode::InvalidArgument, "Metal texture is null"};
        }

        id<MTLDevice> device = NOZZLE_BRIDGE_GET(id<MTLDevice>, get_default_mtl_device());
        if (!device) {
            return Error{ErrorCode::BackendError, "No default Metal device"};
        }

        if (!ensure_pipeline(device)) {
            return Error{ErrorCode::BackendError, "Failed to initialize swap_rb pipeline"};
        }

        MTLTextureDescriptor *desc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:src.pixelFormat
                                                                                         width:width
                                                                                        height:height
                                                                                     mipmapped:NO];
        desc.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;
        desc.resourceOptions = MTLResourceStorageModeShared;

        id<MTLTexture> tmp = [device newTextureWithDescriptor:desc];
        if (!tmp) {
            return Error{ErrorCode::ResourceCreationFailed, "Failed to create temp texture for channel swap"};
        }

        id<MTLCommandBuffer> cmd = [s_queue commandBuffer];
        id<MTLComputeCommandEncoder> enc = [cmd computeCommandEncoder];
        [enc setComputePipelineState:s_pipeline];
        [enc setTexture:src atIndex:0];
        [enc setTexture:tmp atIndex:1];

        MTLSize tg = MTLSizeMake(16, 16, 1);
        [enc dispatchThreadgroups:MTLSizeMake(
            (width + tg.width - 1) / tg.width,
            (height + tg.height - 1) / tg.height,
            1) threadsPerThreadgroup:tg];
        [enc endEncoding];

        id<MTLBlitCommandEncoder> blit = [cmd blitCommandEncoder];
        [blit copyFromTexture:tmp
                  sourceSlice:0
                  sourceLevel:0
                 sourceOrigin:MTLOriginMake(0, 0, 0)
                   sourceSize:MTLSizeMake(width, height, 1)
                    toTexture:src
             destinationSlice:0
             destinationLevel:0
            destinationOrigin:MTLOriginMake(0, 0, 0)];
        [blit endEncoding];

        [cmd commit];
        [cmd waitUntilCompleted];

        return {};
    }
}

} // namespace nozzle::metal
