#import <Metal/Metal.h>
#import <IOSurface/IOSurface.h>
#import <CoreVideo/CoreVideo.h>
#include <cstdio>
#include <cstring>

int main() {
    @autoreleasepool {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (!device) { printf("FAIL: No Metal device\n"); return 1; }
        printf("Metal device: %s\n\n", device.name.UTF8String);

        // RGBA16Unorm = 4ch x 16bit = 8 bytes/pixel
        // Try with 'RGhA' FourCC (same as RGBA16Float, 64-bit RGBA)
        NSDictionary *props = @{
            (id)kIOSurfaceWidth:           @64,
            (id)kIOSurfaceHeight:          @64,
            (id)kIOSurfaceBytesPerElement: @8,
            (id)kIOSurfacePixelFormat:     @(static_cast<OSType>('RGhA')),
        };
        IOSurfaceRef surface = IOSurfaceCreate((CFDictionaryRef)props);
        printf("IOSurface (RGhA): %s\n", surface ? "OK" : "FAIL");
        if (!surface) return 1;

        MTLTextureDescriptor *desc =
            [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA16Unorm
                                                               width:64
                                                              height:64
                                                           mipmapped:NO];
        desc.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;
        desc.storageMode = MTLStorageModeShared;

        id<MTLTexture> texture = nil;
        @try {
            texture = [device newTextureWithDescriptor:desc iosurface:surface plane:0];
        } @catch (NSException *e) {
            printf("MTLTexture: EXCEPTION (%s)\n", e.reason.UTF8String);
            CFRelease(surface);
            return 1;
        }
        printf("MTLTexture (RGBA16Unorm from RGhA IOSurface): %s\n", texture ? "OK" : "FAIL");
        if (!texture) { CFRelease(surface); return 1; }

        // write/read round-trip
        uint32_t pixel_count = 64 * 64;
        uint32_t data_size = pixel_count * 8;
        NSMutableData *fillData = [NSMutableData dataWithLength:data_size];
        memset(fillData.mutableBytes, 0xCD, data_size);

        id<MTLCommandQueue> queue = [device newCommandQueue];
        id<MTLBuffer> srcBuf = [device newBufferWithBytes:fillData.bytes length:data_size options:MTLResourceStorageModeShared];

        id<MTLCommandBuffer> cmdBuf = [queue commandBuffer];
        id<MTLBlitCommandEncoder> blit = [cmdBuf blitCommandEncoder];
        [blit copyFromBuffer:srcBuf sourceOffset:0 sourceBytesPerRow:8*64 sourceBytesPerImage:data_size sourceSize:MTLSizeMake(64,64,1) toTexture:texture destinationSlice:0 destinationLevel:0 destinationOrigin:MTLOriginMake(0,0,0)];
        [blit synchronizeTexture:texture slice:0 level:0];
        [blit endEncoding];
        [cmdBuf commit];
        [cmdBuf waitUntilCompleted];

        id<MTLBuffer> dstBuf = [device newBufferWithLength:data_size options:MTLResourceStorageModeShared];
        id<MTLCommandBuffer> readCmd = [queue commandBuffer];
        id<MTLBlitCommandEncoder> readBlit = [readCmd blitCommandEncoder];
        [readBlit copyFromTexture:texture sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0,0,0) sourceSize:MTLSizeMake(64,64,1) toBuffer:dstBuf destinationOffset:0 destinationBytesPerRow:8*64 destinationBytesPerImage:data_size];
        [readBlit endEncoding];
        [readCmd commit];
        [readCmd waitUntilCompleted];

        bool match = memcmp(srcBuf.contents, dstBuf.contents, data_size) == 0;
        printf("Write/Read: %s\n", match ? "OK" : "MISMATCH");

        CFRelease(surface);
        return match ? 0 : 1;
    }
}
