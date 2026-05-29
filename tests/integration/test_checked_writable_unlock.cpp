// nozzle - test_checked_writable_unlock.cpp - checked writable unlock lifecycle tests

#include <catch2/catch_test_macros.hpp>

#include <nozzle/nozzle_c.h>

namespace {

bool is_backend_unavailable(NozzleErrorCode error) {
    return error == NOZZLE_ERROR_UNSUPPORTED_BACKEND
        || error == NOZZLE_ERROR_RESOURCE_CREATION_FAILED
        || error == NOZZLE_ERROR_BACKEND_ERROR;
}

struct SenderHandle {
    NozzleSender *p{};
    ~SenderHandle() {
        if (p) {
            nozzle_sender_destroy(p);
        }
    }
};

struct ReceiverHandle {
    NozzleReceiver *p{};
    ~ReceiverHandle() {
        if (p) {
            nozzle_receiver_destroy(p);
        }
    }
};

struct FrameHandle {
    NozzleFrame *p{};
    ~FrameHandle() {
        if (p) {
            nozzle_frame_release(p);
        }
    }
};

} // namespace

TEST_CASE("C API: checked writable unlock reports lifecycle errors", "[integration][c_api][pixel_access]") {
    SenderHandle sender;
    NozzleSenderDesc sdesc{};
    sdesc.name = "checked_writable_unlock_lifecycle";
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
    FrameHandle writable{raw_frame};

    // Checked unlock is intentionally not a legacy no-op.  It must report
    // lifecycle misuse when no writable mapping is active on this OS thread.
    CHECK(nozzle_frame_unlock_writable_pixels_checked(writable.p) == NOZZLE_ERROR_INVALID_ARGUMENT);

    NozzleMappedPixels mapped{};
    REQUIRE(nozzle_frame_lock_writable_pixels_with_origin(
        writable.p, NOZZLE_ORIGIN_TOP_LEFT, &mapped) == NOZZLE_OK);
    REQUIRE(mapped.data != nullptr);

    // Pixel lock/unlock state is thread-affine; this test calls checked unlock
    // on the same OS thread that acquired the mapping.
    CHECK(nozzle_frame_unlock_writable_pixels_checked(writable.p) == NOZZLE_OK);
    CHECK(nozzle_frame_unlock_writable_pixels_checked(writable.p) == NOZZLE_ERROR_INVALID_ARGUMENT);
}

TEST_CASE("C API: commit rejects still-mapped writable frame", "[integration][c_api][pixel_access]") {
    SenderHandle sender;
    NozzleSenderDesc sdesc{};
    sdesc.name = "checked_writable_unlock_still_mapped_commit";
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
    FrameHandle writable{raw_frame};

    NozzleMappedPixels mapped{};
    REQUIRE(nozzle_frame_lock_writable_pixels_with_origin(
        writable.p, NOZZLE_ORIGIN_TOP_LEFT, &mapped) == NOZZLE_OK);
    REQUIRE(mapped.data != nullptr);

    CHECK(nozzle_sender_commit_frame(sender.p, writable.p) == NOZZLE_ERROR_INVALID_ARGUMENT);

    REQUIRE(nozzle_frame_unlock_writable_pixels_checked(writable.p) == NOZZLE_OK);
    CHECK(nozzle_sender_commit_frame(sender.p, writable.p) == NOZZLE_OK);
}

TEST_CASE("C API: checked writable unlock rejects non-writable receiver frame", "[integration][c_api][pixel_access]") {
    SenderHandle sender;
    NozzleSenderDesc sdesc{};
    sdesc.name = "checked_writable_unlock_receiver";
    sdesc.application_name = "test";
    sdesc.ring_buffer_size = 2;
    NozzleErrorCode sender_rc = nozzle_sender_create(&sdesc, &sender.p);
    if (is_backend_unavailable(sender_rc)) {
        SKIP("backend device is not available on this runner");
    }
    REQUIRE(sender_rc == NOZZLE_OK);

    NozzleFrame *raw_writable = nullptr;
    NozzleErrorCode frame_rc = nozzle_sender_acquire_writable_frame(
        sender.p, 4, 4, NOZZLE_FORMAT_RGBA8_UNORM, &raw_writable);
    if (is_backend_unavailable(frame_rc)) {
        SKIP("writable frame creation is not available on this runner");
    }
    REQUIRE(frame_rc == NOZZLE_OK);
    FrameHandle writable{raw_writable};
    REQUIRE(nozzle_sender_commit_frame(sender.p, writable.p) == NOZZLE_OK);

    ReceiverHandle receiver;
    NozzleReceiverDesc rdesc{};
    rdesc.name = "checked_writable_unlock_receiver";
    rdesc.application_name = "test";
    REQUIRE(nozzle_receiver_create(&rdesc, &receiver.p) == NOZZLE_OK);

    NozzleAcquireDesc adesc{};
    adesc.timeout_ms = 500;
    NozzleFrame *raw_receiver_frame = nullptr;
    NozzleErrorCode recv_rc = nozzle_receiver_acquire_frame(receiver.p, &adesc, &raw_receiver_frame);
    if (is_backend_unavailable(recv_rc)) {
        SKIP("receiver texture import is not available on this runner");
    }
    REQUIRE(recv_rc == NOZZLE_OK);
    FrameHandle receiver_frame{raw_receiver_frame};

    CHECK(nozzle_frame_unlock_writable_pixels_checked(receiver_frame.p) == NOZZLE_ERROR_INVALID_ARGUMENT);
}

TEST_CASE("C API: discard writable frame releases sender slot without publishing", "[integration][c_api][pixel_access]") {
    SenderHandle sender;
    NozzleSenderDesc sdesc{};
    sdesc.name = "checked_writable_discard_slot";
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
    FrameHandle writable{raw_frame};

    CHECK(nozzle_sender_discard_frame(sender.p, writable.p) == NOZZLE_OK);
    CHECK(nozzle_sender_commit_frame(sender.p, writable.p) == NOZZLE_ERROR_INVALID_ARGUMENT);

    NozzleFrame *raw_second = nullptr;
    frame_rc = nozzle_sender_acquire_writable_frame(
        sender.p, 4, 4, NOZZLE_FORMAT_RGBA8_UNORM, &raw_second);
    REQUIRE(frame_rc == NOZZLE_OK);
    FrameHandle second{raw_second};
    CHECK(nozzle_sender_discard_frame(sender.p, second.p) == NOZZLE_OK);
}
