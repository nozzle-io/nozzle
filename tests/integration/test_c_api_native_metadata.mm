#include <catch2/catch_test_macros.hpp>

#include <nozzle/nozzle_c.h>
#include <nozzle/nozzle.hpp>
#include <nozzle/pixel_access.hpp>
#include <nozzle/backends/metal.hpp>

#import <IOSurface/IOSurface.h>

#include <array>
#include <cstdint>
#include <string>

#include <unistd.h>

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

		NozzleConnectedSenderSnapshot ci{};
		NozzleErrorCode ci_err = nozzle_receiver_get_connected_info_snapshot(receiver, &ci);
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

namespace {
struct external_receiver_handles {
	NozzleReceiver *receiver{nullptr};
	NozzleFrame *frame{nullptr};
	NozzlePixelMapping *mapping{nullptr};
	~external_receiver_handles() {
		if (mapping) nozzle_pixel_mapping_unlock(&mapping);
		if (frame) nozzle_frame_release(frame);
		if (receiver) nozzle_receiver_destroy(receiver);
	}
};

std::array<uint8_t, 4> external_bgra_pixel(uint32_t x, uint32_t y) {
	return {static_cast<uint8_t>(193 - 7 * x - 11 * y),
		static_cast<uint8_t>(41 + 5 * x + 19 * y),
		static_cast<uint8_t>(17 + 13 * x + 3 * y),
		static_cast<uint8_t>(23 + 17 * x + 29 * y)};
}
} // namespace

TEST_CASE("External texture: public BGRA publication preserves raw semantics and pixels",
	"[integration][c_api][metal][shared_layout][external_semantics_runtime]") {
	@autoreleasepool {
		constexpr uint32_t width = 7;
		constexpr uint32_t height = 3;
		nozzle::sender_desc desc{};
		desc.name = "external_semantics_" + std::to_string(::getpid());
		desc.application_name = "external_semantics_tests";
		// Default actual Metal device; missing backend/device is a failure here.
		auto created = nozzle::sender::create(desc);
		INFO(created.error().message);
		REQUIRE(created.ok());
		auto &sender = created.value();
		auto acquired = sender.acquire_writable_frame(
			{width, height, nozzle::texture_format::bgra8_unorm});
		INFO(acquired.error().message);
		REQUIRE(acquired.ok());
		auto &writable = acquired.value();
		const auto &texture = writable.get_texture();
		REQUIRE(texture.valid());
		REQUIRE(texture.desc().format == nozzle::texture_format::bgra8_unorm);
		// Nonfatal: a descriptor regression must not mask raw receiver evidence.
		CHECK(texture.desc().semantic_format == nozzle::texture_format::bgra8_unorm);
		CHECK(texture.resolved().semantic_format == nozzle::texture_format::bgra8_unorm);
		CHECK(texture.resolved().native.backend == nozzle::backend_type::metal);
		CHECK(texture.resolved().native.kind == nozzle::native_format_kind::mtl_pixel_format);
		CHECK(texture.resolved().native.value == static_cast<uint32_t>(MTLPixelFormatBGRA8Unorm));

		auto mapped = nozzle::lock_writable_pixels_mapping_with_origin(
			writable, nozzle::texture_origin::top_left);
		INFO(mapped.error().message);
		REQUIRE(mapped.ok());
		const auto &pixels = mapped.value().pixels();
		REQUIRE(pixels.data != nullptr);
		REQUIRE(pixels.width == width);
		REQUIRE(pixels.height == height);
		REQUIRE(pixels.format == nozzle::texture_format::bgra8_unorm);
		REQUIRE(pixels.origin == nozzle::texture_origin::top_left);
		REQUIRE(pixels.row_stride_bytes >= width * 4);
		for (uint32_t y = 0; y < height; ++y) {
			auto *row = static_cast<uint8_t *>(pixels.data) + y * pixels.row_stride_bytes;
			for (uint32_t x = 0; x < width; ++x) {
				const auto expected = external_bgra_pixel(x, y);
				for (std::size_t channel = 0; channel < expected.size(); ++channel) {
					row[4 * x + channel] = expected[channel];
				}
			}
		}
		auto unlocked = mapped.value().unlock_checked();
		INFO(unlocked.error().message);
		REQUIRE(unlocked.ok());
		auto published = sender.publish_external_texture(texture);
		INFO(published.error().message);
		REQUIRE(published.ok());

		// Independent C API receiver object in this process. Both the sender and
		// its writable texture remain alive through metadata checks and readback.
		external_receiver_handles received;
		NozzleReceiverDesc receiver_desc{};
		receiver_desc.name = desc.name.c_str();
		receiver_desc.application_name = "external_semantics_receiver";
		REQUIRE(nozzle_receiver_create(&receiver_desc, &received.receiver) == NOZZLE_OK);
		NozzleAcquireDesc acquire_desc{};
		acquire_desc.timeout_ms = 1000;
		REQUIRE(nozzle_receiver_acquire_frame(received.receiver, &acquire_desc, &received.frame) == NOZZLE_OK);
		NozzleFrameInfo info{};
		REQUIRE(nozzle_frame_get_info(received.frame, &info) == NOZZLE_OK);
		CHECK(info.frame_index == 1);
		CHECK(info.width == width);
		CHECK(info.height == height);
		CHECK(info.format == NOZZLE_FORMAT_BGRA8_UNORM);
		CHECK(info.semantic_format == NOZZLE_FORMAT_BGRA8_UNORM);
		NozzleResolvedTextureFormat resolved{};
		REQUIRE(nozzle_frame_get_resolved_format(received.frame, &resolved) == NOZZLE_OK);
		CHECK(resolved.storage_format == NOZZLE_FORMAT_BGRA8_UNORM);
		CHECK(resolved.semantic_format == NOZZLE_FORMAT_BGRA8_UNORM);
		CHECK(resolved.native_backend == NOZZLE_BACKEND_METAL);
		CHECK(resolved.native_kind == NOZZLE_NATIVE_KIND_MTL_PIXEL_FORMAT);
		CHECK(resolved.native_value == static_cast<uint32_t>(MTLPixelFormatBGRA8Unorm));

		NozzleMappedPixels readback{};
		REQUIRE(nozzle_frame_lock_pixels_mapping_with_origin(received.frame,
			NOZZLE_ORIGIN_TOP_LEFT, &received.mapping, &readback) == NOZZLE_OK);
		REQUIRE(readback.data != nullptr);
		REQUIRE(readback.width == width);
		REQUIRE(readback.height == height);
		REQUIRE(readback.format == NOZZLE_FORMAT_BGRA8_UNORM);
		REQUIRE(readback.origin == NOZZLE_ORIGIN_TOP_LEFT);
		REQUIRE(readback.row_stride_bytes >= width * 4);
		for (uint32_t y = 0; y < height; ++y) {
			const auto *row = static_cast<const uint8_t *>(readback.data) + y * readback.row_stride_bytes;
			for (uint32_t x = 0; x < width; ++x) {
				const auto expected = external_bgra_pixel(x, y);
				for (std::size_t channel = 0; channel < expected.size(); ++channel) {
					CAPTURE(x, y, channel);
					CHECK(row[4 * x + channel] == expected[channel]);
				}
			}
		}
		REQUIRE(nozzle_pixel_mapping_unlock_checked(&received.mapping) == NOZZLE_OK);
	}
}

TEST_CASE("Writable texture export preserves frame intent across ring reuse",
    "[integration][c_api][metal][writable_export]") {
    for (bool external : {false, true}) {
        DYNAMIC_SECTION((external ? "external texture" : "normal commit")) {
            @autoreleasepool {
                constexpr uint32_t width = 7;
                constexpr uint32_t height = 3;
                const std::string prefix = "export_intent_" + std::to_string(::getpid())
                    + (external ? "_external" : "_commit");
                nozzle::sender_desc owner_desc{};
                owner_desc.name = prefix + "_owner";
                owner_desc.application_name = "export_intent_tests";
                owner_desc.ring_buffer_size = 2;
                auto owner_result = nozzle::sender::create(owner_desc);
                REQUIRE(owner_result.ok());
                auto &owner = owner_result.value();
                nozzle::sender_desc publisher_desc = owner_desc;
                publisher_desc.name = prefix + "_publisher";
                auto publisher_result = nozzle::sender::create(publisher_desc);
                REQUIRE(publisher_result.ok());
                auto &publisher = publisher_result.value();
                const auto &published_name = external ? publisher_desc.name : owner_desc.name;

                // Commit rotates two slots; external publication followed by discard
                // reuses the reserved slot. In both routes compare actual IOSurface IDs.
                std::array<IOSurfaceID, 2> initial_surfaces{};
                for (uint32_t iteration = 0; iteration < 6; ++iteration) {
                    CAPTURE(external, iteration);
                    const bool rgba_intent = iteration < 2 || iteration >= 4;
                    const auto semantic = rgba_intent ? nozzle::texture_format::rgba8_unorm
                                                     : nozzle::texture_format::bgra8_unorm;
                    const auto swizzle = rgba_intent ? nozzle::channel_swizzle::swap_rb
                                                    : nozzle::channel_swizzle::identity;
                    const auto c_semantic = rgba_intent ? NOZZLE_FORMAT_RGBA8_UNORM
                                                       : NOZZLE_FORMAT_BGRA8_UNORM;
                    const auto c_swizzle = rgba_intent ? NOZZLE_CHANNEL_SWIZZLE_SWAP_RB
                                                      : NOZZLE_CHANNEL_SWIZZLE_IDENTITY;
                    nozzle::texture_desc requested{width, height, nozzle::texture_format::bgra8_unorm,
                        semantic, swizzle};
                    auto acquired = owner.acquire_writable_frame(requested);
                    REQUIRE(acquired.ok());
                    auto &writable = acquired.value();
                    const auto &texture = writable.get_texture();
                    CHECK(writable.desc().semantic_format == semantic);
                    CHECK(writable.desc().swizzle == swizzle);
                    // Nonfatal: still collect independent receiver evidence on old code.
                    CHECK(texture.desc().semantic_format == semantic);
                    CHECK(texture.desc().swizzle == swizzle);
                    CHECK(texture.resolved().semantic_format == semantic);
                    CHECK(texture.resolved().swizzle == swizzle);
                    CHECK(texture.resolved().storage_format == nozzle::texture_format::bgra8_unorm);
                    CHECK(texture.resolved().cpu_layout.order == nozzle::channel_order::bgra);
                    CHECK(texture.resolved().native.kind == nozzle::native_format_kind::mtl_pixel_format);
                    CHECK(texture.resolved().native.value == static_cast<uint32_t>(MTLPixelFormatBGRA8Unorm));
                    auto surface = static_cast<IOSurfaceRef>(nozzle::metal::get_io_surface(texture));
                    REQUIRE(surface != nullptr);
                    const auto surface_id = IOSurfaceGetID(surface);
                    REQUIRE(surface_id != 0);
                    const uint32_t slot = external ? 0 : iteration % 2;
                    if (iteration < (external ? 1u : 2u)) initial_surfaces[slot] = surface_id;
                    else CHECK(surface_id == initial_surfaces[slot]);

                    auto mapping = nozzle::lock_writable_pixels_mapping_with_origin(
                        writable, nozzle::texture_origin::top_left);
                    REQUIRE(mapping.ok());
                    const auto &pixels = mapping.value().pixels();
                    REQUIRE(pixels.data != nullptr);
                    REQUIRE(pixels.width == width);
                    REQUIRE(pixels.height == height);
                    REQUIRE(pixels.format == nozzle::texture_format::bgra8_unorm);
                    REQUIRE(pixels.row_stride_bytes >= width * 4);
                    for (uint32_t y = 0; y < height; ++y) {
                        auto *row = static_cast<uint8_t *>(pixels.data) + y * pixels.row_stride_bytes;
                        for (uint32_t x = 0; x < width; ++x) {
                            auto expected = external_bgra_pixel(x, y);
                            // Distinguish consecutive frames as well as channels/alpha.
                            expected[1] = static_cast<uint8_t>(expected[1] + iteration);
                            for (std::size_t c = 0; c < expected.size(); ++c) row[4 * x + c] = expected[c];
                        }
                    }
                    REQUIRE(mapping.value().unlock_checked().ok());
                    if (external) REQUIRE(publisher.publish_external_texture(texture).ok());
                    else REQUIRE(owner.commit_frame(writable).ok());

                    {
                        // A separate C receiver observes raw wire metadata and resolved
                        // native layout. Owners remain alive; release before slot reuse.
                        external_receiver_handles received;
                        NozzleReceiverDesc receiver_desc{};
                        receiver_desc.name = published_name.c_str();
                        receiver_desc.application_name = "export_intent_receiver";
                        REQUIRE(nozzle_receiver_create(&receiver_desc, &received.receiver) == NOZZLE_OK);
                        NozzleAcquireDesc acquire_desc{1000};
                        REQUIRE(nozzle_receiver_acquire_frame(received.receiver, &acquire_desc,
                            &received.frame) == NOZZLE_OK);
                        NozzleFrameInfo info{};
                        REQUIRE(nozzle_frame_get_info(received.frame, &info) == NOZZLE_OK);
                        CHECK(info.frame_index == iteration + 1);
                        CHECK(info.width == width);
                        CHECK(info.height == height);
                        CHECK(info.format == NOZZLE_FORMAT_BGRA8_UNORM);
                        CHECK(info.semantic_format == c_semantic);
                        NozzleResolvedTextureFormat resolved{};
                        REQUIRE(nozzle_frame_get_resolved_format(received.frame, &resolved) == NOZZLE_OK);
                        CHECK(resolved.storage_format == NOZZLE_FORMAT_BGRA8_UNORM);
                        CHECK(resolved.semantic_format == c_semantic);
                        CHECK(resolved.native_backend == NOZZLE_BACKEND_METAL);
                        CHECK(resolved.native_kind == NOZZLE_NATIVE_KIND_MTL_PIXEL_FORMAT);
                        CHECK(resolved.native_value == static_cast<uint32_t>(MTLPixelFormatBGRA8Unorm));
                        CHECK(resolved.channel_order == static_cast<uint32_t>(nozzle::channel_order::bgra));
                        CHECK(resolved.bytes_per_pixel == 4);
                        NozzleFormatFallbackInfo fallback{};
                        REQUIRE(nozzle_frame_get_format_fallback_info(received.frame, &fallback) == NOZZLE_OK);
                        CHECK(fallback.requested_format == c_semantic);
                        CHECK(fallback.storage_format == NOZZLE_FORMAT_BGRA8_UNORM);
                        CHECK(fallback.swizzle == c_swizzle);
                        NozzleConnectedSenderSnapshot connected{};
                        REQUIRE(nozzle_receiver_get_connected_info_snapshot(received.receiver, &connected) == NOZZLE_OK);
                        CHECK(connected.semantic_format == c_semantic);

                        NozzleMappedPixels readback{};
                        REQUIRE(nozzle_frame_lock_pixels_mapping_with_origin(received.frame,
                            NOZZLE_ORIGIN_TOP_LEFT, &received.mapping, &readback) == NOZZLE_OK);
                        REQUIRE(readback.data != nullptr);
                        REQUIRE(readback.width == width);
                        REQUIRE(readback.height == height);
                        REQUIRE(readback.format == NOZZLE_FORMAT_BGRA8_UNORM);
                        REQUIRE(readback.row_stride_bytes >= width * 4);
                        for (uint32_t y = 0; y < height; ++y) {
                            const auto *row = static_cast<const uint8_t *>(readback.data) + y * readback.row_stride_bytes;
                            for (uint32_t x = 0; x < width; ++x) {
                                auto expected = external_bgra_pixel(x, y);
                                expected[1] = static_cast<uint8_t>(expected[1] + iteration);
                                for (std::size_t c = 0; c < expected.size(); ++c) {
                                    CAPTURE(x, y, c);
                                    CHECK(row[4 * x + c] == expected[c]);
                                }
                            }
                        }
                        REQUIRE(nozzle_pixel_mapping_unlock_checked(&received.mapping) == NOZZLE_OK);
                    }
                    if (external) REQUIRE(owner.discard_frame(writable).ok());
                }
            }
        }
    }
}
