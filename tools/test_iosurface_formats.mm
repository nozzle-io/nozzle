// Test IOSurface pixel format support on Apple Silicon
// Compile: clang++ -std=c++17 -fobjc-arc -framework Metal -framework IOSurface -framework CoreVideo -o test_iosurface_formats test_iosurface_formats.mm

#import <Metal/Metal.h>
#import <IOSurface/IOSurface.h>
#import <CoreVideo/CoreVideo.h>
#include <cstdio>
#include <cstring>

struct format_test {
    const char *name;
    OSType cv_format;
    MTLPixelFormat mtl_format;
    uint32_t bpp;  // bytes per pixel
};

static const format_test tests[] = {
    // Currently supported
    {"BGRA8Unorm",    kCVPixelFormatType_32BGRA,          MTLPixelFormatBGRA8Unorm,   4},
    {"RGBA8Unorm",    kCVPixelFormatType_32RGBA,          MTLPixelFormatRGBA8Unorm,   4},
    {"RGBA16Float",   kCVPixelFormatType_64RGBAHalf,      MTLPixelFormatRGBA16Float,  8},

    // New: want to test
    {"R8Unorm",       kCVPixelFormatType_OneComponent8,    MTLPixelFormatR8Unorm,      1},
    {"RG8Unorm",      kCVPixelFormatType_TwoComponent8,    MTLPixelFormatRG8Unorm,     2},
    {"R16Float",      kCVPixelFormatType_OneComponent16Half, MTLPixelFormatR16Float,    2},
    {"RG16Float",     kCVPixelFormatType_TwoComponent16Half, MTLPixelFormatRG16Float,   4},
    {"R32Float",      kCVPixelFormatType_OneComponent32Float, MTLPixelFormatR32Float,   4},
    {"RG32Float",     kCVPixelFormatType_TwoComponent32Float, MTLPixelFormatRG32Float,  8},
    {"RGBA32Float",   kCVPixelFormatType_128RGBAFloat,     MTLPixelFormatRGBA32Float, 16},
};

int main() {
    @autoreleasepool {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (!device) {
            printf("FAIL: No Metal device\n");
            return 1;
        }
        printf("Metal device: %s\n\n", device.name.UTF8String);
        printf("%-16s %-12s %-18s %-12s\n", "Format", "IOSurface", "MTLTexture", "Write/Read");
        printf("%-16s %-12s %-18s %-12s\n", "------", "---------", "----------", "----------");

        for (const auto &t : tests) {
            // 1. Create IOSurface
            NSDictionary *props = @{
                (id)kIOSurfaceWidth:    @64,
                (id)kIOSurfaceHeight:   @64,
                (id)kIOSurfaceBytesPerElement: @(t.bpp),
                (id)kIOSurfacePixelFormat: @(t.cv_format),
            };
            IOSurfaceRef surface = IOSurfaceCreate((CFDictionaryRef)props);

            if (!surface) {
                printf("%-16s %-12s %-18s %-12s\n", t.name, "FAIL", "-", "-");
                continue;
            }

            // 2. Create Metal texture from IOSurface
            MTLTextureDescriptor *desc =
                [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:t.mtl_format
                                                                   width:64
                                                                  height:64
                                                               mipmapped:NO];
            desc.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;
            desc.storageMode = MTLStorageModeShared;

            id<MTLTexture> texture = nil;
            @try {
                texture = [device newTextureWithDescriptor:desc
                                                    iosurface:surface
                                                        plane:0];
            } @catch (NSException *e) {
                printf("%-16s %-12s %-18s %-12s\n", t.name, "OK", "EXCEPTION", "-");
                CFRelease(surface);
                continue;
            }

            if (!texture) {
                printf("%-16s %-12s %-18s %-12s\n", t.name, "OK", "FAIL", "-");
                CFRelease(surface);
                continue;
            }

            // 3. Write + Read test via Metal
            // Create a command buffer, blit some data, read it back
            id<MTLCommandQueue> queue = [device newCommandQueue];
            id<MTLCommandBuffer> cmdBuf = [queue commandBuffer];
            id<MTLBlitCommandEncoder> blit = [cmdBuf blitCommandEncoder];

            // Fill a buffer with known data
            uint32_t pixel_count = 64 * 64;
            uint32_t data_size = pixel_count * t.bpp;
            NSMutableData *fillData = [NSMutableData dataWithLength:data_size];
            memset(fillData.mutableBytes, 0xAB, data_size);

            id<MTLBuffer> srcBuf = [device newBufferWithBytes:fillData.bytes
                                                       length:data_size
                                                      options:MTLResourceStorageModeShared];

            [blit copyFromBuffer:srcBuf
                     sourceOffset:0
                sourceBytesPerRow:t.bpp * 64
              sourceBytesPerImage:data_size
                       sourceSize:MTLSizeMake(64, 64, 1)
                        toTexture:texture
                 destinationSlice:0
                 destinationLevel:0
                destinationOrigin:MTLOriginMake(0, 0, 0)];
            [blit synchronizeTexture:texture slice:0 level:0];
            [blit endEncoding];
            [cmdBuf commit];
            [cmdBuf waitUntilCompleted];

            // Read back
            id<MTLBuffer> dstBuf = [device newBufferWithLength:data_size
                                                       options:MTLResourceStorageModeShared];
            id<MTLCommandBuffer> readCmd = [queue commandBuffer];
            id<MTLBlitCommandEncoder> readBlit = [readCmd blitCommandEncoder];
            [readBlit copyFromTexture:texture
                          sourceSlice:0
                          sourceLevel:0
                         sourceOrigin:MTLOriginMake(0, 0, 0)
                           sourceSize:MTLSizeMake(64, 64, 1)
                             toBuffer:dstBuf
                    destinationOffset:0
               destinationBytesPerRow:t.bpp * 64
             destinationBytesPerImage:data_size];
            [readBlit endEncoding];
            [readCmd commit];
            [readCmd waitUntilCompleted];

            bool match = (memcmp(srcBuf.contents, dstBuf.contents, data_size) == 0);
            printf("%-16s %-12s %-18s %-12s\n",
                   t.name, "OK", "OK", match ? "OK" : "MISMATCH");

            CFRelease(surface);
        }
    }
    return 0;
}
