// nozzle - test_checked_writable_unlock.cpp - checked writable unlock lifecycle tests

#include <catch2/catch_test_macros.hpp>

#include <nozzle/nozzle.hpp>
#include <nozzle/nozzle_c.h>
#include <nozzle/pixel_access.hpp>

#include <thread>

namespace {

bool is_backend_unavailable(NozzleErrorCode error) {
    return error == NOZZLE_ERROR_UNSUPPORTED_BACKEND
        || error == NOZZLE_ERROR_RESOURCE_CREATION_FAILED
        || error == NOZZLE_ERROR_BACKEND_ERROR;
}

bool is_backend_unavailable(nozzle::ErrorCode error) {
    return error == nozzle::ErrorCode::UnsupportedBackend
        || error == nozzle::ErrorCode::ResourceCreationFailed
        || error == nozzle::ErrorCode::BackendError;
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

struct PixelMappingHandle {
    NozzlePixelMapping *p{};
    ~PixelMappingHandle() {
        if (p) {
            nozzle_pixel_mapping_unlock(&p);
        }
    }
};

} // namespace

TEST_CASE("C++ API: moved writable frame retains legacy mapping ownership", "[integration][pixel_access]") {
    nozzle::sender_desc sdesc{};
    sdesc.name = "cpp_legacy_writable_move";
    sdesc.application_name = "test";
    sdesc.ring_buffer_size = 2;
    auto sender_result = nozzle::sender::create(sdesc);
    if (!sender_result.ok() && is_backend_unavailable(sender_result.error().code)) {
        SKIP("backend device is not available on this runner");
    }
    REQUIRE(sender_result.ok());
    auto sender = std::move(sender_result.value());

    nozzle::texture_desc texture_desc{};
    texture_desc.width = 4;
    texture_desc.height = 4;
    texture_desc.format = nozzle::texture_format::rgba8_unorm;

    auto writable_result = sender.acquire_writable_frame(texture_desc);
    if (!writable_result.ok() && is_backend_unavailable(writable_result.error().code)) {
        SKIP("writable frame creation is not available on this runner");
    }
    REQUIRE(writable_result.ok());
    auto writable = std::move(writable_result.value());

    auto lock_result = nozzle::lock_writable_pixels_with_origin(
        writable, nozzle::texture_origin::top_left);
    if (!lock_result.ok() && is_backend_unavailable(lock_result.error().code)) {
        SKIP("legacy writable pixel mapping is not available on this runner");
    }
    REQUIRE(lock_result.ok());
    REQUIRE(lock_result.value().data != nullptr);

    nozzle::writable_frame moved = std::move(writable);
    CHECK(nozzle::unlock_writable_pixels_checked(writable).error().code == nozzle::ErrorCode::InvalidArgument);
    CHECK(nozzle::unlock_writable_pixels_checked(moved).ok());

    auto second_lock = nozzle::lock_writable_pixels_with_origin(
        moved, nozzle::texture_origin::top_left);
    REQUIRE(second_lock.ok());
    nozzle::unlock_writable_pixels(moved);
    CHECK(sender.discard_frame(moved).ok());
}

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

    // Legacy frame-level lock/unlock is retained as a compatibility path.
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

TEST_CASE("C API: pixel mapping handle null and double-unlock semantics", "[integration][c_api][pixel_access]") {
    NozzlePixelMapping *mapping = nullptr;
    CHECK(nozzle_pixel_mapping_unlock_checked(nullptr) == NOZZLE_ERROR_INVALID_ARGUMENT);
    CHECK(nozzle_pixel_mapping_unlock_checked(&mapping) == NOZZLE_ERROR_INVALID_ARGUMENT);
    nozzle_pixel_mapping_unlock(nullptr);
    nozzle_pixel_mapping_unlock(&mapping);
}

TEST_CASE("C API: writable pixel mapping handle owns unlock lifecycle", "[integration][c_api][pixel_access]") {
    SenderHandle sender;
    NozzleSenderDesc sdesc{};
    sdesc.name = "writable_mapping_handle_lifecycle";
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

    PixelMappingHandle mapping;
    NozzleMappedPixels mapped{};
    REQUIRE(nozzle_frame_lock_writable_pixels_mapping_with_origin(
        writable.p, NOZZLE_ORIGIN_TOP_LEFT, &mapping.p, &mapped) == NOZZLE_OK);
    REQUIRE(mapping.p != nullptr);
    REQUIRE(mapped.data != nullptr);

    CHECK(nozzle_sender_commit_frame(sender.p, writable.p) == NOZZLE_ERROR_INVALID_ARGUMENT);
    CHECK(nozzle_pixel_mapping_unlock_checked(&mapping.p) == NOZZLE_OK);
    CHECK(mapping.p == nullptr);
    CHECK(nozzle_pixel_mapping_unlock_checked(&mapping.p) == NOZZLE_ERROR_INVALID_ARGUMENT);
    CHECK(nozzle_sender_commit_frame(sender.p, writable.p) == NOZZLE_OK);
}

TEST_CASE("C API: read pixel mapping handle owns unlock lifecycle", "[integration][c_api][pixel_access]") {
    SenderHandle sender;
    NozzleSenderDesc sdesc{};
    sdesc.name = "read_mapping_handle_lifecycle";
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
    rdesc.name = "read_mapping_handle_lifecycle";
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

    PixelMappingHandle mapping;
    NozzleMappedPixels mapped{};
    NozzleErrorCode lock_rc = nozzle_frame_lock_pixels_mapping_with_origin(
        receiver_frame.p, NOZZLE_ORIGIN_TOP_LEFT, &mapping.p, &mapped);
    if (is_backend_unavailable(lock_rc)) {
        SKIP("read pixel mapping is not available on this runner");
    }
    REQUIRE(lock_rc == NOZZLE_OK);
    REQUIRE(mapping.p != nullptr);
    REQUIRE(mapped.data != nullptr);

    CHECK(nozzle_pixel_mapping_unlock_checked(&mapping.p) == NOZZLE_OK);
    CHECK(mapping.p == nullptr);
    CHECK(nozzle_pixel_mapping_unlock_checked(&mapping.p) == NOZZLE_ERROR_INVALID_ARGUMENT);
}

TEST_CASE("C API: legacy read mapping compatibility slot is frame-owned", "[integration][c_api][pixel_access]") {
    SenderHandle sender;
    NozzleSenderDesc sdesc{};
    sdesc.name = "legacy_read_mapping_frame_slot";
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
    rdesc.name = "legacy_read_mapping_frame_slot";
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

    NozzleMappedPixels mapped{};
    NozzleErrorCode lock_rc = nozzle_frame_lock_pixels_with_origin(
        receiver_frame.p, NOZZLE_ORIGIN_TOP_LEFT, &mapped);
    if (is_backend_unavailable(lock_rc)) {
        SKIP("legacy read pixel mapping is not available on this runner");
    }
    REQUIRE(lock_rc == NOZZLE_OK);
    REQUIRE(mapped.data != nullptr);

    NozzleMappedPixels second_mapped{};
    CHECK(nozzle_frame_lock_pixels_with_origin(
        receiver_frame.p, NOZZLE_ORIGIN_TOP_LEFT, &second_mapped) == NOZZLE_ERROR_INVALID_ARGUMENT);

    // Wrong-frame read unlock is a void legacy no-op and must not consume the
    // active compatibility slot on receiver_frame.
    nozzle_frame_unlock_pixels(writable.p);
    CHECK(nozzle_frame_lock_pixels_with_origin(
        receiver_frame.p, NOZZLE_ORIGIN_TOP_LEFT, &second_mapped) == NOZZLE_ERROR_INVALID_ARGUMENT);

    nozzle_frame_unlock_pixels(receiver_frame.p);
    REQUIRE(nozzle_frame_lock_pixels_with_origin(
        receiver_frame.p, NOZZLE_ORIGIN_TOP_LEFT, &second_mapped) == NOZZLE_OK);
    nozzle_frame_unlock_pixels(receiver_frame.p);
}

TEST_CASE("C API: legacy read mapping unlock can run on another thread", "[integration][c_api][pixel_access]") {
    SenderHandle sender;
    NozzleSenderDesc sdesc{};
    sdesc.name = "legacy_read_mapping_cross_thread";
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
    rdesc.name = "legacy_read_mapping_cross_thread";
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

    NozzleMappedPixels mapped{};
    NozzleErrorCode lock_rc = nozzle_frame_lock_pixels_with_origin(
        receiver_frame.p, NOZZLE_ORIGIN_TOP_LEFT, &mapped);
    if (is_backend_unavailable(lock_rc)) {
        SKIP("legacy read pixel mapping is not available on this runner");
    }
    REQUIRE(lock_rc == NOZZLE_OK);
    REQUIRE(mapped.data != nullptr);

    std::thread unlock_thread([&]() {
        nozzle_frame_unlock_pixels(receiver_frame.p);
    });
    unlock_thread.join();

    NozzleMappedPixels second_mapped{};
    REQUIRE(nozzle_frame_lock_pixels_with_origin(
        receiver_frame.p, NOZZLE_ORIGIN_TOP_LEFT, &second_mapped) == NOZZLE_OK);
    nozzle_frame_unlock_pixels(receiver_frame.p);
}

TEST_CASE("C API: legacy read unlock does not consume independent mapping handle", "[integration][c_api][pixel_access]") {
    SenderHandle sender;
    NozzleSenderDesc sdesc{};
    sdesc.name = "legacy_read_does_not_consume_handle";
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
    rdesc.name = "legacy_read_does_not_consume_handle";
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

    PixelMappingHandle mapping;
    NozzleMappedPixels mapped{};
    NozzleErrorCode lock_rc = nozzle_frame_lock_pixels_mapping_with_origin(
        receiver_frame.p, NOZZLE_ORIGIN_TOP_LEFT, &mapping.p, &mapped);
    if (is_backend_unavailable(lock_rc)) {
        SKIP("read pixel mapping is not available on this runner");
    }
    REQUIRE(lock_rc == NOZZLE_OK);
    REQUIRE(mapping.p != nullptr);
    REQUIRE(mapped.data != nullptr);

    nozzle_frame_unlock_pixels(receiver_frame.p);
    CHECK(nozzle_pixel_mapping_unlock_checked(&mapping.p) == NOZZLE_OK);
    CHECK(mapping.p == nullptr);
}

TEST_CASE("C API: writable pixel mapping handle can unlock on another thread", "[integration][c_api][pixel_access]") {
    SenderHandle sender;
    NozzleSenderDesc sdesc{};
    sdesc.name = "writable_mapping_handle_cross_thread";
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

    NozzlePixelMapping *mapping = nullptr;
    NozzleMappedPixels mapped{};
    REQUIRE(nozzle_frame_lock_writable_pixels_mapping_with_origin(
        writable.p, NOZZLE_ORIGIN_TOP_LEFT, &mapping, &mapped) == NOZZLE_OK);
    REQUIRE(mapping != nullptr);
    REQUIRE(mapped.data != nullptr);

    NozzleErrorCode unlock_rc = NOZZLE_ERROR_UNKNOWN;
    std::thread unlock_thread([&]() {
        unlock_rc = nozzle_pixel_mapping_unlock_checked(&mapping);
    });
    unlock_thread.join();

    CHECK(unlock_rc == NOZZLE_OK);
    CHECK(mapping == nullptr);
    CHECK(nozzle_sender_commit_frame(sender.p, writable.p) == NOZZLE_OK);
}

TEST_CASE("C API: legacy writable mapping can unlock on another thread", "[integration][c_api][pixel_access]") {
    SenderHandle sender;
    NozzleSenderDesc sdesc{};
    sdesc.name = "legacy_writable_mapping_cross_thread";
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

    NozzleErrorCode unlock_rc = NOZZLE_ERROR_UNKNOWN;
    std::thread unlock_thread([&]() {
        unlock_rc = nozzle_frame_unlock_writable_pixels_checked(writable.p);
    });
    unlock_thread.join();

    CHECK(unlock_rc == NOZZLE_OK);
    CHECK(nozzle_sender_commit_frame(sender.p, writable.p) == NOZZLE_OK);
}

TEST_CASE("C API: legacy writable mapping and mapping handle reject mixed ownership", "[integration][c_api][pixel_access]") {
    SenderHandle sender;
    NozzleSenderDesc sdesc{};
    sdesc.name = "legacy_writable_mapping_conflicts";
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

    PixelMappingHandle mapping;
    NozzleMappedPixels mapped{};
    REQUIRE(nozzle_frame_lock_writable_pixels_mapping_with_origin(
        writable.p, NOZZLE_ORIGIN_TOP_LEFT, &mapping.p, &mapped) == NOZZLE_OK);
    REQUIRE(mapping.p != nullptr);
    REQUIRE(mapped.data != nullptr);

    NozzleMappedPixels legacy_mapped{};
    CHECK(nozzle_frame_lock_writable_pixels_with_origin(
        writable.p, NOZZLE_ORIGIN_TOP_LEFT, &legacy_mapped) == NOZZLE_ERROR_INVALID_ARGUMENT);
    CHECK(nozzle_frame_unlock_writable_pixels_checked(writable.p) == NOZZLE_ERROR_INVALID_ARGUMENT);
    CHECK(nozzle_pixel_mapping_unlock_checked(&mapping.p) == NOZZLE_OK);
    CHECK(mapping.p == nullptr);

    REQUIRE(nozzle_frame_lock_writable_pixels_with_origin(
        writable.p, NOZZLE_ORIGIN_TOP_LEFT, &legacy_mapped) == NOZZLE_OK);
    NozzlePixelMapping *second_mapping = nullptr;
    CHECK(nozzle_frame_lock_writable_pixels_mapping_with_origin(
        writable.p, NOZZLE_ORIGIN_TOP_LEFT, &second_mapping, &mapped) == NOZZLE_ERROR_INVALID_ARGUMENT);
    CHECK(second_mapping == nullptr);
    CHECK(nozzle_frame_unlock_writable_pixels_checked(writable.p) == NOZZLE_OK);
    CHECK(nozzle_sender_commit_frame(sender.p, writable.p) == NOZZLE_OK);
}

TEST_CASE("C API: wrong-frame legacy writable unlock does not consume owner mapping", "[integration][c_api][pixel_access]") {
    SenderHandle sender;
    NozzleSenderDesc sdesc{};
    sdesc.name = "legacy_writable_wrong_frame";
    sdesc.application_name = "test";
    sdesc.ring_buffer_size = 2;
    NozzleErrorCode sender_rc = nozzle_sender_create(&sdesc, &sender.p);
    if (is_backend_unavailable(sender_rc)) {
        SKIP("backend device is not available on this runner");
    }
    REQUIRE(sender_rc == NOZZLE_OK);

    NozzleFrame *raw_first = nullptr;
    NozzleErrorCode frame_rc = nozzle_sender_acquire_writable_frame(
        sender.p, 4, 4, NOZZLE_FORMAT_RGBA8_UNORM, &raw_first);
    if (is_backend_unavailable(frame_rc)) {
        SKIP("writable frame creation is not available on this runner");
    }
    REQUIRE(frame_rc == NOZZLE_OK);
    FrameHandle first{raw_first};

    NozzleFrame *raw_second = nullptr;
    REQUIRE(nozzle_sender_acquire_writable_frame(
        sender.p, 4, 4, NOZZLE_FORMAT_RGBA8_UNORM, &raw_second) == NOZZLE_OK);
    FrameHandle second{raw_second};

    NozzleMappedPixels mapped{};
    REQUIRE(nozzle_frame_lock_writable_pixels_with_origin(
        first.p, NOZZLE_ORIGIN_TOP_LEFT, &mapped) == NOZZLE_OK);
    REQUIRE(mapped.data != nullptr);

    CHECK(nozzle_frame_unlock_writable_pixels_checked(second.p) == NOZZLE_ERROR_INVALID_ARGUMENT);
    CHECK(nozzle_sender_commit_frame(sender.p, first.p) == NOZZLE_ERROR_INVALID_ARGUMENT);
    CHECK(nozzle_frame_unlock_writable_pixels_checked(first.p) == NOZZLE_OK);
    CHECK(nozzle_sender_commit_frame(sender.p, first.p) == NOZZLE_OK);
    CHECK(nozzle_sender_discard_frame(sender.p, second.p) == NOZZLE_OK);
}

TEST_CASE("C API: writable pixel mapping handle cleanup survives frame release", "[integration][c_api][pixel_access]") {
    SenderHandle sender;
    NozzleSenderDesc sdesc{};
    sdesc.name = "writable_mapping_handle_frame_release";
    sdesc.application_name = "test";
    sdesc.ring_buffer_size = 2;
    NozzleErrorCode sender_rc = nozzle_sender_create(&sdesc, &sender.p);
    if (is_backend_unavailable(sender_rc)) {
        SKIP("backend device is not available on this runner");
    }
    REQUIRE(sender_rc == NOZZLE_OK);

    NozzleFrame *frame = nullptr;
    NozzleErrorCode frame_rc = nozzle_sender_acquire_writable_frame(
        sender.p, 4, 4, NOZZLE_FORMAT_RGBA8_UNORM, &frame);
    if (is_backend_unavailable(frame_rc)) {
        SKIP("writable frame creation is not available on this runner");
    }
    REQUIRE(frame_rc == NOZZLE_OK);

    PixelMappingHandle mapping;
    NozzleMappedPixels mapped{};
    REQUIRE(nozzle_frame_lock_writable_pixels_mapping_with_origin(
        frame, NOZZLE_ORIGIN_TOP_LEFT, &mapping.p, &mapped) == NOZZLE_OK);
    REQUIRE(mapping.p != nullptr);
    REQUIRE(mapped.data != nullptr);

    nozzle_frame_release(frame);
    frame = nullptr;

    CHECK(nozzle_pixel_mapping_unlock_checked(&mapping.p) == NOZZLE_OK);
    CHECK(mapping.p == nullptr);
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

TEST_CASE("C API: releasing writable frame wrapper does not return sender slot", "[integration][c_api][pixel_access]") {
    SenderHandle sender;
    NozzleSenderDesc sdesc{};
    sdesc.name = "writable_release_does_not_return_slot";
    sdesc.application_name = "test";
    sdesc.ring_buffer_size = 2;
    NozzleErrorCode sender_rc = nozzle_sender_create(&sdesc, &sender.p);
    if (is_backend_unavailable(sender_rc)) {
        SKIP("backend device is not available on this runner");
    }
    REQUIRE(sender_rc == NOZZLE_OK);

    NozzleFrame *raw_first = nullptr;
    NozzleErrorCode frame_rc = nozzle_sender_acquire_writable_frame(
        sender.p, 4, 4, NOZZLE_FORMAT_RGBA8_UNORM, &raw_first);
    if (is_backend_unavailable(frame_rc)) {
        SKIP("writable frame creation is not available on this runner");
    }
    REQUIRE(frame_rc == NOZZLE_OK);
    FrameHandle first{raw_first};

    NozzleFrame *raw_second = nullptr;
    frame_rc = nozzle_sender_acquire_writable_frame(
        sender.p, 4, 4, NOZZLE_FORMAT_RGBA8_UNORM, &raw_second);
    REQUIRE(frame_rc == NOZZLE_OK);
    FrameHandle second{raw_second};

    nozzle_frame_release(first.p);
    first.p = nullptr;

    NozzleFrame *raw_third = nullptr;
    CHECK(nozzle_sender_acquire_writable_frame(
        sender.p, 4, 4, NOZZLE_FORMAT_RGBA8_UNORM, &raw_third) == NOZZLE_ERROR_TIMEOUT);
    CHECK(raw_third == nullptr);

    CHECK(nozzle_sender_discard_frame(sender.p, second.p) == NOZZLE_OK);

    NozzleFrame *raw_after_discard = nullptr;
    REQUIRE(nozzle_sender_acquire_writable_frame(
        sender.p, 4, 4, NOZZLE_FORMAT_RGBA8_UNORM, &raw_after_discard) == NOZZLE_OK);
    FrameHandle after_discard{raw_after_discard};
    CHECK(nozzle_sender_discard_frame(sender.p, after_discard.p) == NOZZLE_OK);
}
