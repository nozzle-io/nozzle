#include <catch2/catch_test_macros.hpp>

#include <nozzle/nozzle.hpp>
#include <nozzle/backends/metal.hpp>

#import <Metal/Metal.h>
#import <IOSurface/IOSurface.h>

static const uint32_t kTestW = 4;
static const uint32_t kTestH = 4;
static const uint32_t kBPP = 4;
static const uint32_t kRowBytes = kTestW * kBPP;

static void fill_texture(id<MTLTexture> tex, uint8_t value) {
	uint8_t data[kTestH * kRowBytes];
	memset(data, value, sizeof(data));
	MTLRegion region = MTLRegionMake2D(0, 0, kTestW, kTestH);
	[tex replaceRegion:region mipmapLevel:0 withBytes:data bytesPerRow:kRowBytes];
}

static void read_texture(id<MTLTexture> tex, uint8_t *out) {
	MTLRegion region = MTLRegionMake2D(0, 0, kTestW, kTestH);
	[tex getBytes:out bytesPerRow:kRowBytes fromRegion:region mipmapLevel:0];
}

static bool verify_fill(const uint8_t *data, size_t len, uint8_t expected) {
	for (size_t i = 0; i < len; ++i) {
		if (data[i] != expected) return false;
	}
	return true;
}

static id<MTLTexture> create_blit_texture(id<MTLDevice> device) {
	MTLTextureDescriptor *desc =
		[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
		                                                   width:kTestW
		                                                  height:kTestH
		                                               mipmapped:NO];
	desc.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;
	desc.resourceOptions = MTLResourceStorageModeShared;
	return [device newTextureWithDescriptor:desc];
}

static id<MTLTexture> create_iosurface_texture(id<MTLDevice> device) {
	uint32_t bytes_per_row = ((kTestW * kBPP) + 63) & ~63u;
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
	NSDictionary *props = @{
		(id)kIOSurfaceIsGlobal:        @(YES),
		(id)kIOSurfaceWidth:           @(kTestW),
		(id)kIOSurfaceHeight:          @(kTestH),
		(id)kIOSurfacePixelFormat:     @(static_cast<uint32_t>('BGRA')),
		(id)kIOSurfaceBytesPerRow:     @(bytes_per_row),
		(id)kIOSurfaceBytesPerElement: @(kBPP),
	};
#pragma clang diagnostic pop
	IOSurfaceRef surface = IOSurfaceCreate((CFDictionaryRef)props);
	if (!surface) return nil;

	MTLTextureDescriptor *desc =
		[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
		                                                   width:kTestW
		                                                  height:kTestH
		                                               mipmapped:NO];
	desc.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;
	desc.resourceOptions = MTLResourceStorageModeShared;

	id<MTLTexture> tex = [device newTextureWithDescriptor:desc iosurface:surface plane:0];
	CFRelease(surface);
	return tex;
}

static nozzle::receiver create_receiver(const char *name) {
	nozzle::receiver_desc rdesc{};
	rdesc.name = name;
	rdesc.application_name = "test";
	auto result = nozzle::receiver::create(rdesc);
	REQUIRE(result.ok());
	return std::move(result.value());
}

TEST_CASE("Metal blit path: receiver gets pixel data published via injected device", "[metal][native_publish]") {
	@autoreleasepool {
		id<MTLDevice> device = MTLCreateSystemDefaultDevice();
		REQUIRE(device != nil);

		id<MTLTexture> src = create_blit_texture(device);
		REQUIRE(src != nil);
		REQUIRE([src iosurface] == nil);
		fill_texture(src, 0xAB);

		const char *name = "test_metal_blit_data";

		nozzle::sender_desc sdesc{};
		sdesc.name = name;
		sdesc.application_name = "test";
		sdesc.ring_buffer_size = 2;
		sdesc.native_device = nozzle::native_device_desc{
			nozzle::backend_type::metal, static_cast<void *>(device), nullptr
		};

		auto sender_result = nozzle::sender::create(sdesc);
		REQUIRE(sender_result.ok());

		REQUIRE(sender_result.value().publish_native_texture(
			static_cast<void *>(src), kTestW, kTestH, nozzle::texture_format::bgra8_unorm).ok());

		auto recv = create_receiver(name);
		auto frame_result = recv.acquire_frame();
		REQUIRE(frame_result.ok());

		auto info = frame_result.value().info();
		REQUIRE(info.width == kTestW);
		REQUIRE(info.height == kTestH);
		REQUIRE(info.format == nozzle::texture_format::bgra8_unorm);
		REQUIRE(info.frame_index > 0);

		auto &frame_texture = frame_result.value().get_texture();
		REQUIRE(frame_texture.valid());
		REQUIRE(frame_texture.resolved().native.backend == nozzle::backend_type::metal);

		id<MTLTexture> dst = create_blit_texture(device);
		REQUIRE(dst != nil);

		REQUIRE(frame_result.value().copy_to_native_texture(
			static_cast<void *>(dst), kTestW, kTestH, nozzle::texture_format::bgra8_unorm).ok());

		uint8_t dst_data[kTestH * kRowBytes];
		read_texture(dst, dst_data);
		REQUIRE(verify_fill(dst_data, sizeof(dst_data), 0xAB));

		[src release];
		[dst release];
		[device release];
	}
}

TEST_CASE("Metal direct path: receiver IOSurface ID matches source", "[metal][native_publish]") {
	@autoreleasepool {
		id<MTLDevice> device = MTLCreateSystemDefaultDevice();
		REQUIRE(device != nil);

		id<MTLTexture> src = create_iosurface_texture(device);
		REQUIRE(src != nil);
		REQUIRE([src iosurface] != nil);
		fill_texture(src, 0xCD);

		IOSurfaceID source_iosurface_id = IOSurfaceGetID([src iosurface]);
		REQUIRE(source_iosurface_id != 0);

		const char *name = "test_metal_direct_iosurface";

		nozzle::sender_desc sdesc{};
		sdesc.name = name;
		sdesc.application_name = "test";
		sdesc.ring_buffer_size = 2;
		sdesc.native_device = nozzle::native_device_desc{
			nozzle::backend_type::metal, static_cast<void *>(device), nullptr
		};

		auto sender_result = nozzle::sender::create(sdesc);
		REQUIRE(sender_result.ok());

		REQUIRE(sender_result.value().publish_native_texture(
			static_cast<void *>(src), kTestW, kTestH, nozzle::texture_format::bgra8_unorm).ok());

		auto recv = create_receiver(name);
		auto frame_result = recv.acquire_frame();
		REQUIRE(frame_result.ok());

		auto info = frame_result.value().info();
		REQUIRE(info.width == kTestW);
		REQUIRE(info.height == kTestH);
		REQUIRE(info.format == nozzle::texture_format::bgra8_unorm);

		auto &frame_texture = frame_result.value().get_texture();
		REQUIRE(frame_texture.valid());
		REQUIRE(frame_texture.resolved().native.backend == nozzle::backend_type::metal);
		REQUIRE(frame_texture.resolved().storage_format == nozzle::texture_format::bgra8_unorm);

		void *recv_surface_ptr = nozzle::metal::get_io_surface(frame_texture);
		REQUIRE(recv_surface_ptr != nullptr);
		IOSurfaceID recv_iosurface_id = IOSurfaceGetID(static_cast<IOSurfaceRef>(recv_surface_ptr));
		REQUIRE(recv_iosurface_id == source_iosurface_id);

		id<MTLTexture> dst = create_blit_texture(device);
		REQUIRE(dst != nil);

		REQUIRE(frame_result.value().copy_to_native_texture(
			static_cast<void *>(dst), kTestW, kTestH, nozzle::texture_format::bgra8_unorm).ok());

		uint8_t dst_data[kTestH * kRowBytes];
		read_texture(dst, dst_data);
		REQUIRE(verify_fill(dst_data, sizeof(dst_data), 0xCD));

		[src release];
		[dst release];
		[device release];
	}
}

TEST_CASE("Metal blit path: source texture data intact after publish", "[metal][native_publish]") {
	@autoreleasepool {
		id<MTLDevice> device = MTLCreateSystemDefaultDevice();
		REQUIRE(device != nil);

		id<MTLTexture> src = create_blit_texture(device);
		REQUIRE(src != nil);
		REQUIRE([src iosurface] == nil);
		fill_texture(src, 0x42);

		const char *name = "test_metal_borrow_blit";

		nozzle::sender_desc sdesc{};
		sdesc.name = name;
		sdesc.application_name = "test";
		sdesc.ring_buffer_size = 2;
		sdesc.native_device = nozzle::native_device_desc{
			nozzle::backend_type::metal, static_cast<void *>(device), nullptr
		};

		auto sender_result = nozzle::sender::create(sdesc);
		REQUIRE(sender_result.ok());

		REQUIRE(sender_result.value().publish_native_texture(
			static_cast<void *>(src), kTestW, kTestH, nozzle::texture_format::bgra8_unorm).ok());

		uint8_t after[kTestH * kRowBytes];
		read_texture(src, after);
		REQUIRE(verify_fill(after, sizeof(after), 0x42));

		[src release];
		[device release];
	}
}

TEST_CASE("Metal direct path: source texture data and IOSurface pointer intact after publish", "[metal][native_publish]") {
	@autoreleasepool {
		id<MTLDevice> device = MTLCreateSystemDefaultDevice();
		REQUIRE(device != nil);

		id<MTLTexture> src = create_iosurface_texture(device);
		REQUIRE(src != nil);
		REQUIRE([src iosurface] != nil);
		fill_texture(src, 0x77);

		IOSurfaceRef surface_before = [src iosurface];
		REQUIRE(surface_before != nil);
		CFRetain(surface_before);

		const char *name = "test_metal_borrow_direct";

		nozzle::sender_desc sdesc{};
		sdesc.name = name;
		sdesc.application_name = "test";
		sdesc.ring_buffer_size = 2;
		sdesc.native_device = nozzle::native_device_desc{
			nozzle::backend_type::metal, static_cast<void *>(device), nullptr
		};

		auto sender_result = nozzle::sender::create(sdesc);
		REQUIRE(sender_result.ok());

		REQUIRE(sender_result.value().publish_native_texture(
			static_cast<void *>(src), kTestW, kTestH, nozzle::texture_format::bgra8_unorm).ok());

		REQUIRE([src iosurface] == surface_before);

		uint8_t after[kTestH * kRowBytes];
		read_texture(src, after);
		REQUIRE(verify_fill(after, sizeof(after), 0x77));

		CFRelease(surface_before);
		[src release];
		[device release];
	}
}

TEST_CASE("Metal ring rotation: receiver gets latest frame with correct payload", "[metal][native_publish]") {
	@autoreleasepool {
		id<MTLDevice> device = MTLCreateSystemDefaultDevice();
		REQUIRE(device != nil);

		const char *name = "test_metal_rotation";

		nozzle::sender_desc sdesc{};
		sdesc.name = name;
		sdesc.application_name = "test";
		sdesc.ring_buffer_size = 2;
		sdesc.native_device = nozzle::native_device_desc{
			nozzle::backend_type::metal, static_cast<void *>(device), nullptr
		};

		auto sender_result = nozzle::sender::create(sdesc);
		REQUIRE(sender_result.ok());

		auto recv = create_receiver(name);

		for (int i = 0; i < 6; ++i) {
			@autoreleasepool {
				id<MTLTexture> src = create_blit_texture(device);
				REQUIRE(src != nil);
				fill_texture(src, static_cast<uint8_t>(0xA0 + i));

				REQUIRE(sender_result.value().publish_native_texture(
					static_cast<void *>(src), kTestW, kTestH, nozzle::texture_format::bgra8_unorm).ok());

				[src release];
			}
		}

		auto frame_result = recv.acquire_frame();
		REQUIRE(frame_result.ok());

		auto info = frame_result.value().info();
		REQUIRE(info.width == kTestW);
		REQUIRE(info.height == kTestH);
		REQUIRE(info.frame_index >= 6);

		id<MTLTexture> dst = create_blit_texture(device);
		REQUIRE(dst != nil);

		REQUIRE(frame_result.value().copy_to_native_texture(
			static_cast<void *>(dst), kTestW, kTestH, nozzle::texture_format::bgra8_unorm).ok());

		uint8_t dst_data[kTestH * kRowBytes];
		read_texture(dst, dst_data);
		REQUIRE(verify_fill(dst_data, sizeof(dst_data), 0xA5));

		[dst release];
		[device release];
	}
}
