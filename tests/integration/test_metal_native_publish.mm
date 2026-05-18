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

static nozzle::sender create_sender(const char *name, id<MTLDevice> device) {
	nozzle::sender_desc sdesc{};
	sdesc.name = name;
	sdesc.application_name = "test";
	sdesc.ring_buffer_size = 2;
	sdesc.native_device = nozzle::native_device_desc{
		nozzle::backend_type::metal, static_cast<void *>(device), nullptr
	};
	auto result = nozzle::sender::create(sdesc);
	REQUIRE(result.ok());
	return std::move(result.value());
}

TEST_CASE("Metal blit: receiver gets pixel data from non-IOSurface texture", "[metal][native_publish]") {
	@autoreleasepool {
		id<MTLDevice> device = MTLCreateSystemDefaultDevice();
		if (device == nil) { SKIP("Metal device is not available on this runner"); }

		id<MTLTexture> src = create_blit_texture(device);
		REQUIRE(src != nil);
		fill_texture(src, 0xAB);

		auto sender = create_sender("test_blit_data", device);
		REQUIRE(sender.publish_native_texture(
			static_cast<void *>(src), kTestW, kTestH, nozzle::texture_format::bgra8_unorm).ok());

		auto recv = create_receiver("test_blit_data");
		auto frame_result = recv.acquire_frame();
		REQUIRE(frame_result.ok());

		auto info = frame_result.value().info();
		REQUIRE(info.width == kTestW);
		REQUIRE(info.height == kTestH);
		REQUIRE(info.frame_index > 0);

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

TEST_CASE("Metal snapshot: IOSurface-backed source is blitted, not shared directly", "[metal][native_publish]") {
	@autoreleasepool {
		id<MTLDevice> device = MTLCreateSystemDefaultDevice();
		if (device == nil) { SKIP("Metal device is not available on this runner"); }

		id<MTLTexture> src = create_iosurface_texture(device);
		REQUIRE(src != nil);
		REQUIRE([src iosurface] != nil);
		fill_texture(src, 0xCD);

		IOSurfaceID source_id = IOSurfaceGetID([src iosurface]);
		REQUIRE(source_id != 0);

		auto sender = create_sender("test_snapshot_iosurface", device);
		REQUIRE(sender.publish_native_texture(
			static_cast<void *>(src), kTestW, kTestH, nozzle::texture_format::bgra8_unorm).ok());

		auto recv = create_receiver("test_snapshot_iosurface");
		auto frame_result = recv.acquire_frame();
		REQUIRE(frame_result.ok());

		auto &frame_texture = frame_result.value().get_texture();
		REQUIRE(frame_texture.valid());

		void *recv_surface_ptr = nozzle::metal::get_io_surface(frame_texture);
		REQUIRE(recv_surface_ptr != nullptr);
		IOSurfaceID recv_id = IOSurfaceGetID(static_cast<IOSurfaceRef>(recv_surface_ptr));

		REQUIRE(recv_id != source_id);

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

TEST_CASE("Metal snapshot: non-IOSurface source mutation after publish does not affect receiver", "[metal][native_publish]") {
	@autoreleasepool {
		id<MTLDevice> device = MTLCreateSystemDefaultDevice();
		if (device == nil) { SKIP("Metal device is not available on this runner"); }

		id<MTLTexture> src = create_blit_texture(device);
		REQUIRE(src != nil);
		fill_texture(src, 0x42);

		auto sender = create_sender("test_snapshot_mutation", device);
		REQUIRE(sender.publish_native_texture(
			static_cast<void *>(src), kTestW, kTestH, nozzle::texture_format::bgra8_unorm).ok());

		fill_texture(src, 0xEE);

		auto recv = create_receiver("test_snapshot_mutation");
		auto frame_result = recv.acquire_frame();
		REQUIRE(frame_result.ok());

		id<MTLTexture> dst = create_blit_texture(device);
		REQUIRE(dst != nil);
		REQUIRE(frame_result.value().copy_to_native_texture(
			static_cast<void *>(dst), kTestW, kTestH, nozzle::texture_format::bgra8_unorm).ok());

		uint8_t dst_data[kTestH * kRowBytes];
		read_texture(dst, dst_data);
		REQUIRE(verify_fill(dst_data, sizeof(dst_data), 0x42));

		[src release];
		[dst release];
		[device release];
	}
}

TEST_CASE("Metal snapshot: IOSurface-backed source mutation after publish does not affect receiver", "[metal][native_publish]") {
	@autoreleasepool {
		id<MTLDevice> device = MTLCreateSystemDefaultDevice();
		if (device == nil) { SKIP("Metal device is not available on this runner"); }

		id<MTLTexture> src = create_iosurface_texture(device);
		REQUIRE(src != nil);
		REQUIRE([src iosurface] != nil);
		fill_texture(src, 0x55);

		auto sender = create_sender("test_snapshot_iosurface_mutation", device);
		REQUIRE(sender.publish_native_texture(
			static_cast<void *>(src), kTestW, kTestH, nozzle::texture_format::bgra8_unorm).ok());

		fill_texture(src, 0xFF);

		auto recv = create_receiver("test_snapshot_iosurface_mutation");
		auto frame_result = recv.acquire_frame();
		REQUIRE(frame_result.ok());

		id<MTLTexture> dst = create_blit_texture(device);
		REQUIRE(dst != nil);
		REQUIRE(frame_result.value().copy_to_native_texture(
			static_cast<void *>(dst), kTestW, kTestH, nozzle::texture_format::bgra8_unorm).ok());

		uint8_t dst_data[kTestH * kRowBytes];
		read_texture(dst, dst_data);
		REQUIRE(verify_fill(dst_data, sizeof(dst_data), 0x55));

		[src release];
		[dst release];
		[device release];
	}
}

TEST_CASE("Metal snapshot: receiver reads frame after source is released", "[metal][native_publish]") {
	@autoreleasepool {
		id<MTLDevice> device = MTLCreateSystemDefaultDevice();
		if (device == nil) { SKIP("Metal device is not available on this runner"); }

		nozzle::sender sender{};
		nozzle::receiver recv{};
		IOSurfaceID source_id = 0;
		{
			id<MTLTexture> src = create_iosurface_texture(device);
			REQUIRE(src != nil);
			fill_texture(src, 0x77);
			source_id = IOSurfaceGetID([src iosurface]);

			sender = create_sender("test_snapshot_release", device);
			REQUIRE(sender.publish_native_texture(
				static_cast<void *>(src), kTestW, kTestH, nozzle::texture_format::bgra8_unorm).ok());

			recv = create_receiver("test_snapshot_release");
			[src release];
		}

		auto frame_result = recv.acquire_frame();
		REQUIRE(frame_result.ok());

		auto &frame_texture = frame_result.value().get_texture();
		REQUIRE(frame_texture.valid());

		void *recv_surface_ptr = nozzle::metal::get_io_surface(frame_texture);
		REQUIRE(recv_surface_ptr != nullptr);
		IOSurfaceID recv_id = IOSurfaceGetID(static_cast<IOSurfaceRef>(recv_surface_ptr));
		REQUIRE(recv_id != source_id);

		id<MTLTexture> dst = create_blit_texture(device);
		REQUIRE(dst != nil);
		REQUIRE(frame_result.value().copy_to_native_texture(
			static_cast<void *>(dst), kTestW, kTestH, nozzle::texture_format::bgra8_unorm).ok());

		uint8_t dst_data[kTestH * kRowBytes];
		read_texture(dst, dst_data);
		REQUIRE(verify_fill(dst_data, sizeof(dst_data), 0x77));

		[dst release];
		[device release];
	}
}

TEST_CASE("Metal ring rotation: receiver gets latest frame with correct payload", "[metal][native_publish]") {
	@autoreleasepool {
		id<MTLDevice> device = MTLCreateSystemDefaultDevice();
		if (device == nil) { SKIP("Metal device is not available on this runner"); }

		auto sender = create_sender("test_rotation", device);
		auto recv = create_receiver("test_rotation");

		for (int i = 0; i < 6; ++i) {
			@autoreleasepool {
				id<MTLTexture> src = create_blit_texture(device);
				REQUIRE(src != nil);
				fill_texture(src, static_cast<uint8_t>(0xA0 + i));

				REQUIRE(sender.publish_native_texture(
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

TEST_CASE("Metal native metadata: publish reports actual MTLPixelFormatBGRA8Unorm", "[metal][native_publish]") {
	@autoreleasepool {
		id<MTLDevice> device = MTLCreateSystemDefaultDevice();
		if (device == nil) { SKIP("Metal device is not available on this runner"); }

		id<MTLTexture> src = create_blit_texture(device);
		REQUIRE(src != nil);
		fill_texture(src, 0xCC);

		auto sender = create_sender("test_native_meta", device);
		REQUIRE(sender.publish_native_texture(
			static_cast<void *>(src), kTestW, kTestH, nozzle::texture_format::bgra8_unorm).ok());

		auto recv = create_receiver("test_native_meta");
		auto frame_result = recv.acquire_frame();
		REQUIRE(frame_result.ok());

		auto &frame = frame_result.value();
		auto &resolved = frame.get_texture().resolved();

		REQUIRE(resolved.native.backend == nozzle::backend_type::metal);
		REQUIRE(resolved.native.kind == nozzle::native_format_kind::mtl_pixel_format);
		REQUIRE(resolved.native.value == static_cast<uint32_t>(MTLPixelFormatBGRA8Unorm));
		REQUIRE(resolved.source == nozzle::format_source::native_observed);

		auto ci = recv.connected_info();
		REQUIRE(ci.native_format_kind == static_cast<uint8_t>(nozzle::native_format_kind::mtl_pixel_format));
		REQUIRE(ci.native_format_value == static_cast<uint32_t>(MTLPixelFormatBGRA8Unorm));

		[src release];
		[device release];
	}
}

TEST_CASE("Metal native metadata: receiver frame texture matches actual MTLPixelFormat", "[metal][native_publish]") {
	@autoreleasepool {
		id<MTLDevice> device = MTLCreateSystemDefaultDevice();
		if (device == nil) { SKIP("Metal device is not available on this runner"); }

		auto sender = create_sender("test_native_recv", device);

		// rgba8_unorm falls back to bgra8_unorm for IOSurface (Apple Silicon limitation)
		auto wf = sender.acquire_writable_frame({
			kTestW, kTestH, nozzle::texture_format::rgba8_unorm
		});
		REQUIRE(wf.ok());

		// verify ring texture itself reports actual BGRA8, not the requested RGBA8
		auto &ring_resolved = wf.value().get_texture().resolved();
		REQUIRE(ring_resolved.native.backend == nozzle::backend_type::metal);
		REQUIRE(ring_resolved.native.kind == nozzle::native_format_kind::mtl_pixel_format);
		REQUIRE(ring_resolved.native.value == static_cast<uint32_t>(MTLPixelFormatBGRA8Unorm));

		sender.commit_frame(wf.value());

		auto recv = create_receiver("test_native_recv");
		auto frame_result = recv.acquire_frame();
		REQUIRE(frame_result.ok());

		auto &resolved = frame_result.value().get_texture().resolved();
		REQUIRE(resolved.native.backend == nozzle::backend_type::metal);
		REQUIRE(resolved.native.kind == nozzle::native_format_kind::mtl_pixel_format);
		REQUIRE(resolved.native.value == static_cast<uint32_t>(MTLPixelFormatBGRA8Unorm));

		[device release];
	}
}

TEST_CASE("Metal device validation: same-device texture passes validation", "[metal][native_publish]") {
	@autoreleasepool {
		id<MTLDevice> device = MTLCreateSystemDefaultDevice();
		if (device == nil) { SKIP("Metal device is not available on this runner"); }

		id<MTLTexture> src = create_blit_texture(device);
		REQUIRE(src != nil);
		fill_texture(src, 0xBB);

		auto sender = create_sender("test_device_match", device);
		REQUIRE(sender.publish_native_texture(
			static_cast<void *>(src), kTestW, kTestH, nozzle::texture_format::bgra8_unorm).ok());

		auto recv = create_receiver("test_device_match");
		auto frame_result = recv.acquire_frame();
		REQUIRE(frame_result.ok());

		id<MTLTexture> dst = create_blit_texture(device);
		REQUIRE(dst != nil);
		REQUIRE(frame_result.value().copy_to_native_texture(
			static_cast<void *>(dst), kTestW, kTestH, nozzle::texture_format::bgra8_unorm).ok());

		uint8_t dst_data[kTestH * kRowBytes];
		read_texture(dst, dst_data);
		REQUIRE(verify_fill(dst_data, sizeof(dst_data), 0xBB));

		[src release];
		[dst release];
		[device release];
	}
}

TEST_CASE("Metal native metadata: wrap_texture reports actual MTLPixelFormat from existing texture", "[metal][native_publish]") {
	@autoreleasepool {
		id<MTLDevice> device = MTLCreateSystemDefaultDevice();
		if (device == nil) { SKIP("Metal device is not available on this runner"); }

		id<MTLTexture> tex = create_blit_texture(device);
		REQUIRE(tex != nil);

		nozzle::metal::texture_wrap_desc wrap_desc{};
		wrap_desc.texture = static_cast<void *>(tex);
		wrap_desc.format = static_cast<uint32_t>(nozzle::texture_format::bgra8_unorm);
		wrap_desc.width = kTestW;
		wrap_desc.height = kTestH;

		auto wrap_result = nozzle::metal::wrap_texture(wrap_desc);
		REQUIRE(wrap_result.ok());

		auto &resolved = wrap_result.value().resolved();
		REQUIRE(resolved.native.backend == nozzle::backend_type::metal);
		REQUIRE(resolved.native.kind == nozzle::native_format_kind::mtl_pixel_format);
		REQUIRE(resolved.native.value == static_cast<uint32_t>(tex.pixelFormat));
		REQUIRE(resolved.native.value == static_cast<uint32_t>(MTLPixelFormatBGRA8Unorm));
		REQUIRE(resolved.source == nozzle::format_source::native_observed);

		[tex release];
		[device release];
	}
}
