#include <catch2/catch_test_macros.hpp>

#include <nozzle/nozzle.hpp>

#import <Metal/Metal.h>
#import <IOSurface/IOSurface.h>

static id<MTLTexture> create_non_iosurface_texture(id<MTLDevice> device, uint32_t w, uint32_t h) {
	MTLTextureDescriptor *desc =
		[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
		                                                   width:w
		                                                  height:h
		                                               mipmapped:NO];
	desc.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;
	desc.resourceOptions = MTLResourceStorageModeManaged;
	return [device newTextureWithDescriptor:desc];
}

static id<MTLTexture> create_iosurface_texture(id<MTLDevice> device, uint32_t w, uint32_t h) {
	uint32_t bytes_per_row = ((w * 4) + 63) & ~63u;
	NSDictionary *props = @{
		(id)kIOSurfaceIsGlobal:        @(YES),
		(id)kIOSurfaceWidth:           @(w),
		(id)kIOSurfaceHeight:          @(h),
		(id)kIOSurfacePixelFormat:     @(static_cast<uint32_t>('BGRA')),
		(id)kIOSurfaceBytesPerRow:     @(bytes_per_row),
		(id)kIOSurfaceBytesPerElement: @(4u),
	};
	IOSurfaceRef surface = IOSurfaceCreate((CFDictionaryRef)props);
	if (!surface) return nil;

	MTLTextureDescriptor *desc =
		[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
		                                                   width:w
		                                                  height:h
		                                               mipmapped:NO];
	desc.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;
	desc.resourceOptions = MTLResourceStorageModeShared;

	id<MTLTexture> tex = [device newTextureWithDescriptor:desc iosurface:surface plane:0];
	CFRelease(surface);
	return tex;
}

TEST_CASE("Metal publish: non-IOSurface blit with injected device", "[metal][native_publish]") {
	id<MTLDevice> device = MTLCreateSystemDefaultDevice();
	REQUIRE(device != nil);

	id<MTLTexture> src = create_non_iosurface_texture(device, 64, 64);
	REQUIRE(src != nil);

	nozzle::sender_desc desc{};
	desc.name = "test_metal_blit";
	desc.application_name = "test";
	desc.ring_buffer_size = 2;
	desc.native_device = nozzle::native_device_desc{
		nozzle::backend_type::metal, static_cast<void *>(device), nullptr
	};

	auto sender_result = nozzle::sender::create(desc);
	REQUIRE(sender_result.ok());

	auto pub_result = sender_result.value().publish_native_texture(
		static_cast<void *>(src), 64, 64, nozzle::texture_format::bgra8_unorm);
	REQUIRE(pub_result.ok());

	REQUIRE(src != nil);
	[device release];
}

TEST_CASE("Metal publish: IOSurface direct path borrows source texture", "[metal][native_publish]") {
	id<MTLDevice> device = MTLCreateSystemDefaultDevice();
	REQUIRE(device != nil);

	id<MTLTexture> src = create_iosurface_texture(device, 64, 64);
	REQUIRE(src != nil);
	REQUIRE([src iosurface] != nil);

	nozzle::sender_desc desc{};
	desc.name = "test_metal_direct";
	desc.application_name = "test";
	desc.ring_buffer_size = 2;
	desc.native_device = nozzle::native_device_desc{
		nozzle::backend_type::metal, static_cast<void *>(device), nullptr
	};

	auto sender_result = nozzle::sender::create(desc);
	REQUIRE(sender_result.ok());

	@autoreleasepool {
		auto pub_result = sender_result.value().publish_native_texture(
			static_cast<void *>(src), 64, 64, nozzle::texture_format::bgra8_unorm);
		REQUIRE(pub_result.ok());
	}

	REQUIRE(src != nil);
	REQUIRE([src iosurface] != nil);

	[device release];
}

TEST_CASE("Metal publish: multiple publishes do not leak or crash", "[metal][native_publish]") {
	id<MTLDevice> device = MTLCreateSystemDefaultDevice();
	REQUIRE(device != nil);

	nozzle::sender_desc desc{};
	desc.name = "test_metal_multi";
	desc.application_name = "test";
	desc.ring_buffer_size = 3;
	desc.native_device = nozzle::native_device_desc{
		nozzle::backend_type::metal, static_cast<void *>(device), nullptr
	};

	auto sender_result = nozzle::sender::create(desc);
	REQUIRE(sender_result.ok());

	for (int i = 0; i < 10; ++i) {
		@autoreleasepool {
			id<MTLTexture> src = create_non_iosurface_texture(device, 32, 32);
			REQUIRE(src != nil);

			auto pub_result = sender_result.value().publish_native_texture(
				static_cast<void *>(src), 32, 32, nozzle::texture_format::bgra8_unorm);
			REQUIRE(pub_result.ok());
		}
	}

	[device release];
}

TEST_CASE("Metal publish: default device (no injection) works for blit path", "[metal][native_publish]") {
	id<MTLTexture> src = create_non_iosurface_texture(MTLCreateSystemDefaultDevice(), 64, 64);
	REQUIRE(src != nil);

	nozzle::sender_desc desc{};
	desc.name = "test_metal_default_blit";
	desc.application_name = "test";
	desc.ring_buffer_size = 2;

	auto sender_result = nozzle::sender::create(desc);
	if (!sender_result.ok()) {
		[src release];
		return;
	}

	auto pub_result = sender_result.value().publish_native_texture(
		static_cast<void *>(src), 64, 64, nozzle::texture_format::bgra8_unorm);
	REQUIRE(pub_result.ok());

	[src release];
}

TEST_CASE("Metal publish: IOSurface direct path metadata uses actual surface", "[metal][native_publish]") {
	id<MTLDevice> device = MTLCreateSystemDefaultDevice();
	REQUIRE(device != nil);

	id<MTLTexture> src = create_iosurface_texture(device, 64, 64);
	REQUIRE(src != nil);

	IOSurfaceRef surface = [src iosurface];
	REQUIRE(surface != nil);
	IOSurfaceID expected_id = IOSurfaceGetID(surface);

	nozzle::sender_desc desc{};
	desc.name = "test_metal_direct_meta";
	desc.application_name = "test";
	desc.ring_buffer_size = 2;
	desc.native_device = nozzle::native_device_desc{
		nozzle::backend_type::metal, static_cast<void *>(device), nullptr
	};

	auto sender_result = nozzle::sender::create(desc);
	REQUIRE(sender_result.ok());

	@autoreleasepool {
		auto pub_result = sender_result.value().publish_native_texture(
			static_cast<void *>(src), 64, 64, nozzle::texture_format::bgra8_unorm);
		REQUIRE(pub_result.ok());
	}

	REQUIRE([src iosurface] != nil);

	[device release];
}
