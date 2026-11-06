// nozzle - test_integration_fallback_getters.cpp - C API fallback info receiver getter (GPU required)

#include <catch2/catch_test_macros.hpp>

#include <nozzle/nozzle_c.h>

#if NOZZLE_HAS_DMA_BUF
#include <cerrno>
#include <sys/stat.h>
#endif

namespace {
#if NOZZLE_HAS_DMA_BUF
bool is_dma_buf_device_absent() {
    struct stat info{};
    if (::stat("/dev/dri/renderD128", &info) == 0) {
        return false;
    }
    return errno == ENOENT || errno == ENOTDIR;
}
#endif

bool is_backend_unavailable(NozzleErrorCode error) {
    if (error == NOZZLE_ERROR_UNSUPPORTED_BACKEND) {
        return true;
    }
#if NOZZLE_HAS_DMA_BUF
    return is_dma_buf_device_absent()
        && (error == NOZZLE_ERROR_RESOURCE_CREATION_FAILED
            || error == NOZZLE_ERROR_BACKEND_ERROR);
#else
    return false;
#endif
}

NozzleSenderDesc make_safe_fallback_sender_desc() {
    NozzleSenderDesc desc{};
    desc.fallback_flags = NOZZLE_FALLBACK_SAFE_DEFAULTS;
    desc.fallback_flags_valid = 1;
    return desc;
}

struct SenderHandle {
    NozzleSender *p{};
    ~SenderHandle() { if (p) nozzle_sender_destroy(p); }
};
struct ReceiverHandle {
    NozzleReceiver *p{};
    ~ReceiverHandle() { if (p) nozzle_receiver_destroy(p); }
};
struct FrameHandle {
    NozzleFrame *p{};
    ~FrameHandle() { if (p) nozzle_frame_release(p); }
};
} // anonymous namespace

TEST_CASE("Integration: receiver_get_connected_format_fallback_info returns connected fallback", "[integration][fallback][writable_frame]") {
    SenderHandle sender;
    NozzleSenderDesc sdesc = make_safe_fallback_sender_desc();
    sdesc.name = "int_fb_recv_test";
    sdesc.application_name = "test";
    sdesc.ring_buffer_size = 2;
    NozzleErrorCode sender_rc = nozzle_sender_create(&sdesc, &sender.p);
    if (is_backend_unavailable(sender_rc)) {
        SKIP("backend device is not available on this runner");
    }
    REQUIRE(sender_rc == NOZZLE_OK);

    NozzleFrame *raw_frame = nullptr;
    NozzleErrorCode frame_rc = nozzle_sender_acquire_writable_frame(
        sender.p, 4, 4, NOZZLE_FORMAT_RGBA8_UNORM, &raw_frame);

    if (is_backend_unavailable(frame_rc)) {
        SKIP("writable frame creation is not available on this runner");
    }
    REQUIRE(frame_rc == NOZZLE_OK);
    FrameHandle frame{raw_frame};
    REQUIRE(nozzle_sender_commit_frame(sender.p, frame.p) == NOZZLE_OK);

    ReceiverHandle receiver;
    NozzleReceiverDesc rdesc{};
    rdesc.name = "int_fb_recv_test";
    rdesc.application_name = "test";
    REQUIRE(nozzle_receiver_create(&rdesc, &receiver.p) == NOZZLE_OK);

    NozzleAcquireDesc adesc{};
    adesc.timeout_ms = 500;
    NozzleFrame *raw_rframe = nullptr;
    NozzleErrorCode receiver_frame_rc =
        nozzle_receiver_acquire_frame(receiver.p, &adesc, &raw_rframe);
    if (is_backend_unavailable(receiver_frame_rc)) {
        SKIP("receiver texture import is not available on this runner");
    }
    REQUIRE(receiver_frame_rc == NOZZLE_OK);
    FrameHandle rframe{raw_rframe};

    NozzleFormatFallbackInfo info{};
    REQUIRE(nozzle_receiver_get_connected_format_fallback_info(receiver.p, &info) == NOZZLE_OK);

    CHECK(info.requested_format == NOZZLE_FORMAT_RGBA8_UNORM);
#if NOZZLE_HAS_METAL
    CHECK(info.storage_format == NOZZLE_FORMAT_BGRA8_UNORM);
    CHECK(info.fallback_target == NOZZLE_FORMAT_BGRA8_UNORM);
    CHECK(info.category == NOZZLE_FALLBACK_CATEGORY_STORAGE_COMPATIBLE);
    CHECK(info.swizzle == NOZZLE_CHANNEL_SWIZZLE_SWAP_RB);
#else
    CHECK(info.storage_format == NOZZLE_FORMAT_RGBA8_UNORM);
    CHECK(info.fallback_target == NOZZLE_FORMAT_UNKNOWN);
    CHECK(info.category == NOZZLE_FALLBACK_CATEGORY_NONE);
    CHECK(info.swizzle == NOZZLE_CHANNEL_SWIZZLE_IDENTITY);
#endif
    CHECK(info.quality_loss == 0);
}
