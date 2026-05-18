#include <catch2/catch_test_macros.hpp>

#include <nozzle/nozzle_c.h>

#import <Metal/Metal.h>

static const uint32_t kTestW = 4;
static const uint32_t kTestH = 4;

static NozzleSender *create_c_sender(const char *name, id<MTLDevice> device) {
	NozzleSenderDesc sdesc{};
	sdesc.name = name;
	sdesc.application_name = "test";
	sdesc.ring_buffer_size = 2;
	NozzleNativeDevice ndesc{};
	ndesc.backend = NOZZLE_BACKEND_METAL;
	ndesc.device = static_cast<void *>(device);
	NozzleSender *sender = nullptr;
	NozzleErrorCode err = nozzle_sender_create_with_native_device(&sdesc, &ndesc, &sender);
	REQUIRE(err == NOZZLE_OK);
	return sender;
}

static id<MTLTexture> create_bgra8_texture(id<MTLDevice> device) {
	MTLTextureDescriptor *desc =
		[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
		                                                   width:kTestW
		                                                  height:kTestH
		                                               mipmapped:NO];
	desc.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;
	desc.resourceOptions = MTLResourceStorageModeShared;
	return [device newTextureWithDescriptor:desc];
}

TEST_CASE("C API: connected sender info exposes native format kind and value", "[c_api][native_format]") {
	@autoreleasepool {
		id<MTLDevice> device = MTLCreateSystemDefaultDevice();
		if (device == nil) { SKIP("Metal device is not available on this runner"); }

		NozzleSender *sender = create_c_sender("test_c_native_meta", device);
		REQUIRE(sender != nullptr);

		id<MTLTexture> src = create_bgra8_texture(device);
		REQUIRE(src != nil);

		uint8_t data[kTestH * kTestW * 4];
		memset(data, 0xDD, sizeof(data));
		MTLRegion region = MTLRegionMake2D(0, 0, kTestW, kTestH);
		[src replaceRegion:region mipmapLevel:0 withBytes:data bytesPerRow:kTestW * 4];

		NozzleErrorCode pub_err = nozzle_sender_publish_native_texture(
			sender, static_cast<void *>(src), kTestW, kTestH, NOZZLE_FORMAT_BGRA8_UNORM);
		REQUIRE(pub_err == NOZZLE_OK);

		NozzleReceiverDesc rdesc{};
		rdesc.name = "test_c_native_meta";
		rdesc.application_name = "test";
		NozzleReceiver *receiver = nullptr;
		NozzleErrorCode recv_err = nozzle_receiver_create(&rdesc, &receiver);
		REQUIRE(recv_err == NOZZLE_OK);

		NozzleAcquireDesc adesc{};
		adesc.timeout_ms = 1000;
		NozzleFrame *frame = nullptr;
		NozzleErrorCode frame_err = nozzle_receiver_acquire_frame(receiver, &adesc, &frame);
		REQUIRE(frame_err == NOZZLE_OK);

		NozzleConnectedSenderInfo ci{};
		NozzleErrorCode ci_err = nozzle_receiver_get_connected_info(receiver, &ci);
		REQUIRE(ci_err == NOZZLE_OK);
		CHECK(ci.native_format_kind == NOZZLE_NATIVE_KIND_MTL_PIXEL_FORMAT);
		CHECK(ci.native_format_value == static_cast<uint32_t>(MTLPixelFormatBGRA8Unorm));

		nozzle_frame_release(frame);
		nozzle_receiver_destroy(receiver);
		nozzle_sender_destroy(sender);
		[src release];
		[device release];
	}
}
