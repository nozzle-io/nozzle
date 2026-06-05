#include <catch2/catch_test_macros.hpp>

#include <nozzle/sender.hpp>
#include <nozzle/types.hpp>
#include <nozzle/nozzle_c.h>

#include <Metal/Metal.h>

TEST_CASE("C++ sender: native_device() returns injected Metal device", "[native_device]") {
    id<MTLDevice> mtl_device = MTLCreateSystemDefaultDevice();
    if (mtl_device == nil) { SKIP("Metal device is not available on this runner"); }

    nozzle::sender_desc desc{};
    desc.name = "test_native_inject";
    desc.native_device.backend = nozzle::backend_type::metal;
    desc.native_device.device = static_cast<void *>(mtl_device);
    desc.native_device.context = nullptr;

    auto r = nozzle::sender::create(desc);
    REQUIRE(r.ok());

    auto nd = r.value().native_device();
    REQUIRE(nd.device == static_cast<void *>(mtl_device));
    REQUIRE(nd.backend == nozzle::backend_type::metal);

    [mtl_device release];
}

TEST_CASE("C++ sender: destruction with injected Metal device does not crash", "[native_device]") {
    id<MTLDevice> mtl_device = MTLCreateSystemDefaultDevice();
    if (mtl_device == nil) { SKIP("Metal device is not available on this runner"); }

    {
        nozzle::sender_desc desc{};
        desc.name = "test_native_destruction";
        desc.native_device.backend = nozzle::backend_type::metal;
        desc.native_device.device = static_cast<void *>(mtl_device);
        desc.native_device.context = nullptr;

        auto r = nozzle::sender::create(desc);
        REQUIRE(r.ok());
    }

    [mtl_device release];
}

TEST_CASE("C API: nozzle_sender_create_with_native_device returns injected device", "[c_api][native_device]") {
    id<MTLDevice> mtl_device = MTLCreateSystemDefaultDevice();
    if (mtl_device == nil) { SKIP("Metal device is not available on this runner"); }

    NozzleSenderDesc desc{};
    desc.name = "test_c_native_inject";

    NozzleNativeDevice native_dev{};
    native_dev.backend = NOZZLE_BACKEND_METAL;
    native_dev.device = static_cast<void *>(mtl_device);
    native_dev.context = nullptr;

    NozzleSender *sender = nullptr;
    NozzleErrorCode ec = nozzle_sender_create_with_native_device(&desc, &native_dev, &sender);
    REQUIRE(ec == NOZZLE_OK);

    NozzleNativeDevice nd{};
    ec = nozzle_sender_get_native_device(sender, &nd);
    REQUIRE(ec == NOZZLE_OK);
    REQUIRE(nd.device == static_cast<void *>(mtl_device));
    REQUIRE(nd.backend == NOZZLE_BACKEND_METAL);

    nozzle_sender_destroy(sender);
    [mtl_device release];
}

TEST_CASE("C API: Metal sender with safe fallbacks can acquire, map, and commit an rgba8 writable frame", "[c_api][metal][writable]") {
    id<MTLDevice> mtl_device = MTLCreateSystemDefaultDevice();
    if (mtl_device == nil) {
        SKIP("Metal device is not available on this runner");
    }
    [mtl_device release];

    nozzle::sender_desc cpp_desc{};
    cpp_desc.name = "test_cpp_metal_default_writable_rgba8";
    cpp_desc.application_name = "test";
    cpp_desc.ring_buffer_size = 2;

    auto sender_result = nozzle::sender::create(cpp_desc);
    if (!sender_result.ok() &&
        (sender_result.error().code == nozzle::ErrorCode::BackendError ||
         sender_result.error().code == nozzle::ErrorCode::ResourceCreationFailed ||
         sender_result.error().code == nozzle::ErrorCode::UnsupportedBackend)) {
        SKIP("default Metal sender creation is not available on this runner: " << sender_result.error().message);
    }
    INFO("sender_create error: " << sender_result.error().message);
    REQUIRE(sender_result.ok());

    auto cpp_sender = std::move(sender_result.value());
    nozzle::texture_desc texture_desc{};
    texture_desc.width = 320;
    texture_desc.height = 240;
    texture_desc.format = nozzle::texture_format::rgba8_unorm;
    auto writable_result = cpp_sender.acquire_writable_frame(texture_desc);
    INFO("acquire_writable_frame error: " << writable_result.error().message);
    REQUIRE(writable_result.ok());
    auto cpp_writable = std::move(writable_result.value());
    REQUIRE(cpp_sender.discard_frame(cpp_writable).ok());

    NozzleSenderDesc desc{};
    desc.name = "test_metal_default_writable_rgba8";
    desc.application_name = "test";
    desc.ring_buffer_size = 2;
    desc.fallback_flags_valid = 1;
    desc.fallback_flags = NOZZLE_FALLBACK_SAFE_DEFAULTS;

    NozzleSender *sender = nullptr;
    NozzleErrorCode error = nozzle_sender_create(&desc, &sender);
    if (error == NOZZLE_ERROR_BACKEND_ERROR ||
        error == NOZZLE_ERROR_RESOURCE_CREATION_FAILED ||
        error == NOZZLE_ERROR_UNSUPPORTED_BACKEND) {
        SKIP("default Metal sender creation is not available on this runner");
    }
    REQUIRE(error == NOZZLE_OK);
    REQUIRE(sender != nullptr);

    NozzleFrame *frame = nullptr;
    error = nozzle_sender_acquire_writable_frame(
        sender,
        320,
        240,
        NOZZLE_FORMAT_RGBA8_UNORM,
        &frame);
    REQUIRE(error == NOZZLE_OK);
    REQUIRE(frame != nullptr);

    NozzlePixelMapping *mapping = nullptr;
    NozzleMappedPixels pixels{};
    error = nozzle_frame_lock_writable_pixels_mapping_with_origin(
        frame,
        NOZZLE_ORIGIN_TOP_LEFT,
        &mapping,
        &pixels);
    REQUIRE(error == NOZZLE_OK);
    REQUIRE(mapping != nullptr);
    REQUIRE(pixels.data != nullptr);
    REQUIRE(pixels.row_stride_bytes >= 320 * 4);

    uint8_t *row = static_cast<uint8_t *>(pixels.data);
    for (uint32_t y = 0; y < 240; ++y) {
        for (uint32_t x = 0; x < 320; ++x) {
            uint8_t *pixel = row + y * pixels.row_stride_bytes + x * 4;
            pixel[0] = static_cast<uint8_t>(x & 0xffu);
            pixel[1] = static_cast<uint8_t>(y & 0xffu);
            pixel[2] = 0x7f;
            pixel[3] = 0xff;
        }
    }

    nozzle_pixel_mapping_unlock(&mapping);
    REQUIRE(mapping == nullptr);
    REQUIRE(nozzle_sender_commit_frame(sender, frame) == NOZZLE_OK);
    nozzle_frame_release(frame);
    nozzle_sender_destroy(sender);
}
