#include <catch2/catch_test_macros.hpp>

#include <nozzle/sender.hpp>
#include <nozzle/types.hpp>
#include <nozzle/nozzle_c.h>

#include <Metal/Metal.h>

TEST_CASE("C++ sender: native_device() returns injected Metal device", "[native_device]") {
    id<MTLDevice> mtl_device = MTLCreateSystemDefaultDevice();
    REQUIRE(mtl_device != nil);

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
    REQUIRE(mtl_device != nil);

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
    REQUIRE(mtl_device != nil);

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
