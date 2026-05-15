// nozzle - test_integration_fallback_getters.cpp - C API fallback info receiver getter (GPU required)

#include <catch2/catch_test_macros.hpp>

#include <nozzle/nozzle_c.h>

TEST_CASE("Integration: receiver_get_connected_format_fallback_info returns connected fallback", "[integration][fallback]") {
    NozzleSender *sender = nullptr;
    NozzleSenderDesc sdesc{};
    sdesc.name = "int_fb_recv_test";
    sdesc.application_name = "test";
    sdesc.ring_buffer_size = 2;
    REQUIRE(nozzle_sender_create(&sdesc, &sender) == NOZZLE_OK);

    NozzleFrame *frame = nullptr;
    NozzleErrorCode frame_rc = nozzle_sender_acquire_writable_frame(
        sender, 4, 4, NOZZLE_FORMAT_RGBA8_UNORM, &frame);

    if (frame_rc != NOZZLE_OK) {
        nozzle_sender_destroy(sender);
        SKIP("GPU not available for writable frame creation");
    }

    REQUIRE(nozzle_sender_commit_frame(sender, frame) == NOZZLE_OK);

    NozzleReceiver *receiver = nullptr;
    NozzleReceiverDesc rdesc{};
    rdesc.name = "int_fb_recv_test";
    rdesc.application_name = "test";
    REQUIRE(nozzle_receiver_create(&rdesc, &receiver) == NOZZLE_OK);

    NozzleAcquireDesc adesc{};
    adesc.timeout_ms = 500;
    NozzleFrame *rframe = nullptr;
    REQUIRE(nozzle_receiver_acquire_frame(receiver, &adesc, &rframe) == NOZZLE_OK);
    nozzle_frame_release(rframe);

    NozzleFormatFallbackInfo info{};
    REQUIRE(nozzle_receiver_get_connected_format_fallback_info(receiver, &info) == NOZZLE_OK);

    CHECK(info.requested_format == NOZZLE_FORMAT_RGBA8_UNORM);
    CHECK(info.storage_format == NOZZLE_FORMAT_RGBA8_UNORM);
    CHECK(info.category == NOZZLE_FALLBACK_CATEGORY_NONE);
    CHECK(info.swizzle == NOZZLE_CHANNEL_SWIZZLE_IDENTITY);
    CHECK(info.quality_loss == 0);

    nozzle_receiver_destroy(receiver);
    nozzle_sender_destroy(sender);
}
