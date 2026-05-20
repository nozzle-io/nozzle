#if NOZZLE_HAS_METAL

#include <nozzle/nozzle.hpp>
#include <nozzle/backends/metal.hpp>

#import <Metal/Metal.h>
#import <IOSurface/IOSurface.h>

#include <cstdio>
#include <cstring>
#include <thread>
#include <chrono>
#include <utility>

namespace {

constexpr uint32_t k_width = 4;
constexpr uint32_t k_height = 4;
constexpr uint32_t k_bytes_per_pixel = 4;
constexpr uint32_t k_row_bytes = k_width * k_bytes_per_pixel;

id<MTLTexture> create_iosurface_texture(id<MTLDevice> device) {
	uint32_t bytes_per_row = ((k_width * k_bytes_per_pixel) + 63) & ~63u;
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
	NSDictionary *props = @{
		(id)kIOSurfaceIsGlobal:        @(YES),
		(id)kIOSurfaceWidth:           @(k_width),
		(id)kIOSurfaceHeight:          @(k_height),
		(id)kIOSurfacePixelFormat:     @(static_cast<uint32_t>('BGRA')),
		(id)kIOSurfaceBytesPerRow:     @(bytes_per_row),
		(id)kIOSurfaceBytesPerElement: @(k_bytes_per_pixel),
	};
#pragma clang diagnostic pop
	IOSurfaceRef surface = IOSurfaceCreate((CFDictionaryRef)props);
	if (!surface) {
		return nil;
	}

	MTLTextureDescriptor *desc =
		[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
		                                                   width:k_width
		                                                  height:k_height
		                                               mipmapped:NO];
	desc.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;
	desc.resourceOptions = MTLResourceStorageModeShared;

	id<MTLTexture> tex = [device newTextureWithDescriptor:desc iosurface:surface plane:0];
	CFRelease(surface);
	return tex;
}

void fill_texture(id<MTLTexture> tex, uint8_t value) {
	uint8_t data[k_height * k_row_bytes];
	std::memset(data, value, sizeof(data));
	MTLRegion region = MTLRegionMake2D(0, 0, k_width, k_height);
	[tex replaceRegion:region mipmapLevel:0 withBytes:data bytesPerRow:k_row_bytes];
}

bool write_ready_file(const char *path, uint64_t iosurface_id) {
	std::FILE *file = std::fopen(path, "w");
	if (!file) {
		return false;
	}
	std::fprintf(file, "%llu\n", static_cast<unsigned long long>(iosurface_id));
	std::fclose(file);
	return true;
}

int run_sender(const char *sender_name, const char *ready_file) {
	@autoreleasepool {
		id<MTLDevice> device = MTLCreateSystemDefaultDevice();
		if (device == nil) {
			return 10;
		}

		nozzle::sender_desc sender_desc{};
		sender_desc.name = sender_name;
		sender_desc.application_name = "metal_direct_cross_process_sender";
		sender_desc.ring_buffer_size = 2;
		sender_desc.native_device = nozzle::native_device_desc{
			nozzle::backend_type::metal, static_cast<void *>(device), nullptr
		};

		auto sender_result = nozzle::sender::create(sender_desc);
		if (!sender_result.ok()) {
			[device release];
			return 11;
		}
		auto sender = std::move(sender_result.value());

		id<MTLTexture> texture = create_iosurface_texture(device);
		if (texture == nil || texture.iosurface == nil) {
			[device release];
			return 12;
		}
		fill_texture(texture, 0xC3);
		IOSurfaceID source_id = IOSurfaceGetID(texture.iosurface);
		if (source_id == 0) {
			[texture release];
			[device release];
			return 13;
		}

		nozzle::metal::direct_publish_desc desc{};
		desc.texture = static_cast<void *>(texture);
		desc.width = k_width;
		desc.height = k_height;
		desc.storage_format = nozzle::texture_format::bgra8_unorm;
		desc.semantic_format = nozzle::texture_format::bgra8_unorm;
		desc.swizzle = nozzle::channel_swizzle::identity;
		auto publish_result = sender.publish_metal_texture_direct(desc);
		[texture release];
		if (!publish_result.ok()) {
			[device release];
			return 14;
		}

		if (!write_ready_file(ready_file, static_cast<uint64_t>(source_id))) {
			[device release];
			return 15;
		}

		std::this_thread::sleep_for(std::chrono::seconds(30));
		[device release];
		return 0;
	}
}

} // namespace

int main(int argc, char **argv) {
	if (argc != 3) {
		return 2;
	}
	return run_sender(argv[1], argv[2]);
}

#else
int main() { return 0; }
#endif
