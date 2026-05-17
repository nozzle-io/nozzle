#include <catch2/catch_test_macros.hpp>

#include <nozzle/nozzle.hpp>

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

static bool read_texture(id<MTLTexture> tex, uint8_t *out) {
	MTLRegion region = MTLRegionMake2D(0, 0, kTestW, kTestH);
	[tex getBytes:out bytesPerRow:kRowBytes fromRegion:region mipmapLevel:0];
	return true;
}

static id<MTLTexture> create_non_iosurface_texture(id<MTLDevice> device) {
	MTLTextureDescriptor *desc =
		[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
		                                                   width:kTestW
		                                                  height:kTestH
		                                               mipmapped:NO];
	desc.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;
	desc.resourceOptions = MTLResourceStorageModeManaged;
	return [device newTextureWithDescriptor:desc];
}

static id<MTLTexture> create_iosurface_texture(id<MTLDevice> device, IOSurfaceID *out_id = nullptr) {
	uint32_t bytes_per_row = ((kTestW * kBPP) + 63) & ~63u;
	NSDictionary *props = @{
		(id)kIOSurfaceIsGlobal:        @(YES),
		(id)kIOSurfaceWidth:           @(kTestW),
		(id)kIOSurfaceHeight:          @(kTestH),
		(id)kIOSurfacePixelFormat:     @(static_cast<uint32_t>('BGRA')),
		(id)kIOSurfaceBytesPerRow:     @(bytes_per_row),
		(id)kIOSurfaceBytesPerElement: @(kBPP),
	};
	IOSurfaceRef surface = IOSurfaceCreate((CFDictionaryRef)props);
	if (!surface) return nil;

	if (out_id) *out_id = IOSurfaceGetID(surface);

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

TEST_CASE("Metal blit path: receiver acquires frame with correct pixel data", "[metal][native_publish]") {
	@autoreleasepool {
		id<MTLDevice> device = MTLCreateSystemDefaultDevice();
		REQUIRE(device != nil);

		id<MTLTexture> src = create_non_iosurface_texture(device);
		REQUIRE(src != nil);
		fill_texture(src, 0xAB);

		const char *sender_name = "test_metal_blit_roundtrip";

		nozzle::sender_desc sdesc{};
		sdesc.name = sender_name;
		sdesc.application_name = "test";
		sdesc.ring_buffer_size = 2;
		sdesc.native_device = nozzle::native_device_desc{
			nozzle::backend_type::metal, static_cast<void *>(device), nullptr
		};

		auto sender_result = nozzle::sender::create(sdesc);
		REQUIRE(sender_result.ok());

		auto pub_result = sender_result.value().publish_native_texture(
			static_cast<void *>(src), kTestW, kTestH, nozzle::texture_format::bgra8_unorm);
		REQUIRE(pub_result.ok());

		nozzle::receiver_desc rdesc{};
		rdesc.name = sender_name;
		rdesc.application_name = "test";

		auto recv_result = nozzle::receiver::create(rdesc);
		if (!recv_result.ok()) {
			[device release];
			return;
		}

		auto frame_result = recv_result.value().acquire_frame();
		REQUIRE(frame_result.ok());

		auto info = frame_result.value().info();
		REQUIRE(info.width == kTestW);
		REQUIRE(info.height == kTestH);
		REQUIRE(info.format == nozzle::texture_format::bgra8_unorm);

		id<MTLTexture> dst = create_non_iosurface_texture(device);
		REQUIRE(dst != nil);

		auto copy_result = frame_result.value().copy_to_native_texture(
			static_cast<void *>(dst), kTestW, kTestH, nozzle::texture_format::bgra8_unorm);
		REQUIRE(copy_result.ok());

		uint8_t dst_data[kTestH * kRowBytes];
		read_texture(dst, dst_data);

		uint8_t expected = 0xAB;
		bool match = true;
		for (size_t i = 0; i < sizeof(dst_data); ++i) {
			if (dst_data[i] != expected) { match = false; break; }
		}
		REQUIRE(match);

		[device release];
	}
}

TEST_CASE("Metal direct path: receiver gets the source IOSurface ID", "[metal][native_publish]") {
	@autoreleasepool {
		id<MTLDevice> device = MTLCreateSystemDefaultDevice();
		REQUIRE(device != nil);

		IOSurfaceID source_iosurface_id = 0;
		id<MTLTexture> src = create_iosurface_texture(device, &source_iosurface_id);
		REQUIRE(src != nil);
		REQUIRE(source_iosurface_id != 0);

		fill_texture(src, 0xCD);

		const char *sender_name = "test_metal_direct_iosurface";

		nozzle::sender_desc sdesc{};
		sdesc.name = sender_name;
		sdesc.application_name = "test";
		sdesc.ring_buffer_size = 2;
		sdesc.native_device = nozzle::native_device_desc{
			nozzle::backend_type::metal, static_cast<void *>(device), nullptr
		};

		auto sender_result = nozzle::sender::create(sdesc);
		REQUIRE(sender_result.ok());

		auto pub_result = sender_result.value().publish_native_texture(
			static_cast<void *>(src), kTestW, kTestH, nozzle::texture_format::bgra8_unorm);
		REQUIRE(pub_result.ok());

		nozzle::receiver_desc rdesc{};
		rdesc.name = sender_name;
		rdesc.application_name = "test";

		auto recv_result = nozzle::receiver::create(rdesc);
		if (!recv_result.ok()) {
			[device release];
			return;
		}

		auto frame_result = recv_result.value().acquire_frame();
		REQUIRE(frame_result.ok());

		auto &tex = frame_result.value().get_texture();
		REQUIRE(tex.valid());

		auto resolved = tex.resolved();
		REQUIRE(resolved.native.backend == nozzle::backend_type::metal);
		REQUIRE(resolved.storage_format == nozzle::texture_format::bgra8_unorm);

		[device release];
	}
}

TEST_CASE("Metal blit path: source texture data intact after publish", "[metal][native_publish]") {
	@autoreleasepool {
		id<MTLDevice> device = MTLCreateSystemDefaultDevice();
		REQUIRE(device != nil);

		id<MTLTexture> src = create_non_iosurface_texture(device);
		REQUIRE(src != nil);
		fill_texture(src, 0x42);

		const char *sender_name = "test_metal_borrow";

		nozzle::sender_desc sdesc{};
		sdesc.name = sender_name;
		sdesc.application_name = "test";
		sdesc.ring_buffer_size = 2;
		sdesc.native_device = nozzle::native_device_desc{
			nozzle::backend_type::metal, static_cast<void *>(device), nullptr
		};

		auto sender_result = nozzle::sender::create(sdesc);
		REQUIRE(sender_result.ok());

		auto pub_result = sender_result.value().publish_native_texture(
			static_cast<void *>(src), kTestW, kTestH, nozzle::texture_format::bgra8_unorm);
		REQUIRE(pub_result.ok());

		uint8_t after[kTestH * kRowBytes];
		read_texture(src, after);

		bool intact = true;
		for (size_t i = 0; i < sizeof(after); ++i) {
			if (after[i] != 0x42) { intact = false; break; }
		}
		REQUIRE(intact);

		[device release];
	}
}

TEST_CASE("Metal direct path: source texture data intact after publish", "[metal][native_publish]") {
	@autoreleasepool {
		id<MTLDevice> device = MTLCreateSystemDefaultDevice();
		REQUIRE(device != nil);

		id<MTLTexture> src = create_iosurface_texture(device);
		REQUIRE(src != nil);
		fill_texture(src, 0x77);

		IOSurfaceRef surface_before = [src iosurface];
		REQUIRE(surface_before != nil);
		CFRetain(surface_before);

		const char *sender_name = "test_metal_direct_borrow";

		nozzle::sender_desc sdesc{};
		sdesc.name = sender_name;
		sdesc.application_name = "test";
		sdesc.ring_buffer_size = 2;
		sdesc.native_device = nozzle::native_device_desc{
			nozzle::backend_type::metal, static_cast<void *>(device), nullptr
		};

		auto sender_result = nozzle::sender::create(sdesc);
		REQUIRE(sender_result.ok());

		auto pub_result = sender_result.value().publish_native_texture(
			static_cast<void *>(src), kTestW, kTestH, nozzle::texture_format::bgra8_unorm);
		REQUIRE(pub_result.ok());

		IOSurfaceRef surface_after = [src iosurface];
		REQUIRE(surface_after != nil);
		REQUIRE(surface_after == surface_before);

		uint8_t after[kTestH * kRowBytes];
		read_texture(src, after);

		bool intact = true;
		for (size_t i = 0; i < sizeof(after); ++i) {
			if (after[i] != 0x77) { intact = false; break; }
		}
		REQUIRE(intact);

		CFRelease(surface_before);
		[device release];
	}
}

TEST_CASE("Metal: ring buffer rotation across multiple publishes", "[metal][native_publish]") {
	@autoreleasepool {
		id<MTLDevice> device = MTLCreateSystemDefaultDevice();
		REQUIRE(device != nil);

		const char *sender_name = "test_metal_rotation";

		nozzle::sender_desc sdesc{};
		sdesc.name = sender_name;
		sdesc.application_name = "test";
		sdesc.ring_buffer_size = 2;
		sdesc.native_device = nozzle::native_device_desc{
			nozzle::backend_type::metal, static_cast<void *>(device), nullptr
		};

		auto sender_result = nozzle::sender::create(sdesc);
		REQUIRE(sender_result.ok());

		nozzle::receiver_desc rdesc{};
		rdesc.name = sender_name;
		rdesc.application_name = "test";

		auto recv_result = nozzle::receiver::create(rdesc);
		if (!recv_result.ok()) {
			[device release];
			return;
		}

		for (int i = 0; i < 6; ++i) {
			@autoreleasepool {
				id<MTLTexture> src = create_non_iosurface_texture(device);
				REQUIRE(src != nil);
				uint8_t fill_val = static_cast<uint8_t>(0xA0 + i);
				fill_texture(src, fill_val);

				auto pub_result = sender_result.value().publish_native_texture(
					static_cast<void *>(src), kTestW, kTestH, nozzle::texture_format::bgra8_unorm);
				REQUIRE(pub_result.ok());
			}
		}

		auto frame_result = recv_result.value().acquire_frame();
		REQUIRE(frame_result.ok());

		auto info = frame_result.value().info();
		REQUIRE(info.width == kTestW);
		REQUIRE(info.height == kTestH);
		REQUIRE(info.frame_index > 0);

		[device release];
	}
}
