// nozzle - test_error_codes.cpp - C API error code enum mapping regression tests

#include <catch2/catch_test_macros.hpp>

#include <nozzle/nozzle_c.h>
#include <nozzle/result.hpp>

#include <cstdint>

#if NOZZLE_ENABLE_TEST_HOOKS
extern "C" void nozzle_test_mark_writable_frame_cpu_unlock_failed(NozzleFrame *frame);
extern "C" void nozzle_test_fail_next_writable_frame_wrapper_alloc(void);
#endif

TEST_CASE("C API: NozzleErrorCode enum values match C++ ErrorCode", "[c_api][error_codes]") {
    CHECK(NOZZLE_OK == static_cast<int>(nozzle::ErrorCode::Ok));
    CHECK(NOZZLE_ERROR_UNKNOWN == static_cast<int>(nozzle::ErrorCode::Unknown));
    CHECK(NOZZLE_ERROR_INVALID_ARGUMENT == static_cast<int>(nozzle::ErrorCode::InvalidArgument));
    CHECK(NOZZLE_ERROR_UNSUPPORTED_BACKEND == static_cast<int>(nozzle::ErrorCode::UnsupportedBackend));
    CHECK(NOZZLE_ERROR_UNSUPPORTED_FORMAT == static_cast<int>(nozzle::ErrorCode::UnsupportedFormat));
    CHECK(NOZZLE_ERROR_DEVICE_MISMATCH == static_cast<int>(nozzle::ErrorCode::DeviceMismatch));
    CHECK(NOZZLE_ERROR_RESOURCE_CREATION_FAILED == static_cast<int>(nozzle::ErrorCode::ResourceCreationFailed));
    CHECK(NOZZLE_ERROR_SHARED_HANDLE_FAILED == static_cast<int>(nozzle::ErrorCode::SharedHandleFailed));
    CHECK(NOZZLE_ERROR_SENDER_NOT_FOUND == static_cast<int>(nozzle::ErrorCode::SenderNotFound));
    CHECK(NOZZLE_ERROR_SENDER_CLOSED == static_cast<int>(nozzle::ErrorCode::SenderClosed));
    CHECK(NOZZLE_ERROR_TIMEOUT == static_cast<int>(nozzle::ErrorCode::Timeout));
    CHECK(NOZZLE_ERROR_BACKEND_ERROR == static_cast<int>(nozzle::ErrorCode::BackendError));
    CHECK(NOZZLE_ERROR_COMMAND_FAILED == static_cast<int>(nozzle::ErrorCode::CommandFailed));
}

TEST_CASE("C API: checked writable unlock rejects null frame", "[c_api][pixel_access]") {
    CHECK(nozzle_frame_unlock_writable_pixels_checked(nullptr) == NOZZLE_ERROR_INVALID_ARGUMENT);
}

#if NOZZLE_ENABLE_TEST_HOOKS

namespace {

bool is_backend_unavailable_for_commit_lifecycle(NozzleErrorCode error) {
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

struct FrameHandle {
    NozzleFrame *p{};
    ~FrameHandle() {
        if (p) {
            nozzle_frame_release(p);
        }
    }
};

} // namespace


TEST_CASE("C API: writable acquire wrapper allocation failure discards sender slot", "[c_api][pixel_access]") {
    SenderHandle sender;
    NozzleSenderDesc sdesc{};
    sdesc.name = "writable_wrapper_alloc_failure_discard";
    sdesc.application_name = "test";
    sdesc.ring_buffer_size = 2;
    NozzleErrorCode sender_rc = nozzle_sender_create(&sdesc, &sender.p);
    if (is_backend_unavailable_for_commit_lifecycle(sender_rc)) {
        SKIP("backend device is not available on this runner");
    }
    REQUIRE(sender_rc == NOZZLE_OK);

    NozzleFrame *failed_frame = reinterpret_cast<NozzleFrame *>(static_cast<uintptr_t>(0x1));
    nozzle_test_fail_next_writable_frame_wrapper_alloc();
    CHECK(nozzle_sender_acquire_writable_frame(
        sender.p, 4, 4, NOZZLE_FORMAT_RGBA8_UNORM, &failed_frame)
        == NOZZLE_ERROR_RESOURCE_CREATION_FAILED);
    CHECK(failed_frame == nullptr);

    NozzleFrame *raw_first = nullptr;
    REQUIRE(nozzle_sender_acquire_writable_frame(
        sender.p, 4, 4, NOZZLE_FORMAT_RGBA8_UNORM, &raw_first) == NOZZLE_OK);
    FrameHandle first{raw_first};

    NozzleFrame *raw_second = nullptr;
    REQUIRE(nozzle_sender_acquire_writable_frame(
        sender.p, 4, 4, NOZZLE_FORMAT_RGBA8_UNORM, &raw_second) == NOZZLE_OK);
    FrameHandle second{raw_second};

    CHECK(nozzle_sender_discard_frame(sender.p, first.p) == NOZZLE_OK);
    CHECK(nozzle_sender_discard_frame(sender.p, second.p) == NOZZLE_OK);
}

TEST_CASE("C API: commit rejects failed writable unlock state", "[c_api][pixel_access]") {
    SenderHandle sender;
    NozzleSenderDesc sdesc{};
    sdesc.name = "checked_writable_unlock_failed_commit";
    sdesc.application_name = "test";
    sdesc.ring_buffer_size = 2;
    NozzleErrorCode sender_rc = nozzle_sender_create(&sdesc, &sender.p);
    if (is_backend_unavailable_for_commit_lifecycle(sender_rc)) {
        SKIP("backend device is not available on this runner");
    }
    REQUIRE(sender_rc == NOZZLE_OK);

    NozzleFrame *raw_frame = nullptr;
    NozzleErrorCode frame_rc = nozzle_sender_acquire_writable_frame(
        sender.p, 4, 4, NOZZLE_FORMAT_RGBA8_UNORM, &raw_frame);
    if (is_backend_unavailable_for_commit_lifecycle(frame_rc)) {
        SKIP("writable frame creation is not available on this runner");
    }
    REQUIRE(frame_rc == NOZZLE_OK);
    FrameHandle writable{raw_frame};

    nozzle_test_mark_writable_frame_cpu_unlock_failed(writable.p);
    CHECK(nozzle_sender_commit_frame(sender.p, writable.p) == NOZZLE_ERROR_BACKEND_ERROR);
    CHECK(nozzle_sender_commit_frame(sender.p, writable.p) == NOZZLE_ERROR_INVALID_ARGUMENT);
}

#endif // NOZZLE_ENABLE_TEST_HOOKS
