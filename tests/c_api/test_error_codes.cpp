// nozzle - test_error_codes.cpp - C API error code enum mapping regression tests

#include <catch2/catch_test_macros.hpp>

#include <nozzle/nozzle_c.h>
#include <nozzle/result.hpp>

#include "nozzle_c_types.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <limits>

#if NOZZLE_ENABLE_TEST_HOOKS
extern "C" void nozzle_test_mark_writable_frame_cpu_unlock_failed(NozzleFrame *frame);
extern "C" void nozzle_test_fail_next_writable_frame_wrapper_alloc(void);
extern "C" void nozzle_test_fail_next_c_api_wrapper_object_alloc(void);
extern "C" void nozzle_test_clear_c_api_wrapper_object_alloc_failure(void);
extern "C" NozzleErrorCode nozzle_test_copy_mapped_pixels_to_buffer(
    const void *source_data,
    int64_t source_row_stride_bytes,
    uint32_t width,
    uint32_t height,
    NozzleTextureFormat format,
    NozzleTextureOrigin origin,
    uint8_t bytes_per_pixel,
    void *out_data,
    uint64_t out_data_size_bytes,
    NozzleMappedPixels *out_pixels
);
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

TEST_CASE("C API: read-only pixel copy rejects invalid arguments", "[c_api][pixel_access]") {
    uint8_t data[4]{};
    NozzleFrame frame{};
    NozzleMappedPixels mapped{};

    CHECK(nozzle_frame_copy_pixels_with_origin(
        nullptr, NOZZLE_ORIGIN_TOP_LEFT, data, sizeof(data), &mapped)
        == NOZZLE_ERROR_INVALID_ARGUMENT);

    CHECK(nozzle_frame_copy_pixels_with_origin(
        &frame,
        NOZZLE_ORIGIN_TOP_LEFT,
        nullptr,
        sizeof(data),
        &mapped)
        == NOZZLE_ERROR_INVALID_ARGUMENT);

    CHECK(nozzle_frame_copy_pixels_with_origin(
        &frame,
        NOZZLE_ORIGIN_TOP_LEFT,
        data,
        sizeof(data),
        nullptr)
        == NOZZLE_ERROR_INVALID_ARGUMENT);
}

TEST_CASE("C API: frame info rejects null and unbacked wrappers", "[c_api][frame_info]") {
    NozzleFrameInfo frame_info{};
    NozzleResolvedTextureFormat resolved{};
    NozzleFormatFallbackInfo fallback{};
    NozzleFrame frame{};
    NozzleFrame writable_frame{};
    writable_frame.is_writable = true;

    CHECK(nozzle_frame_get_info(nullptr, &frame_info) == NOZZLE_ERROR_INVALID_ARGUMENT);
    CHECK(nozzle_frame_get_info(&frame, nullptr) == NOZZLE_ERROR_INVALID_ARGUMENT);
    CHECK(nozzle_frame_get_info(&frame, &frame_info) == NOZZLE_ERROR_INVALID_ARGUMENT);
    CHECK(nozzle_frame_get_info(&writable_frame, &frame_info) == NOZZLE_ERROR_INVALID_ARGUMENT);

    CHECK(nozzle_frame_get_resolved_format(nullptr, &resolved) == NOZZLE_ERROR_INVALID_ARGUMENT);
    CHECK(nozzle_frame_get_resolved_format(&frame, nullptr) == NOZZLE_ERROR_INVALID_ARGUMENT);
    CHECK(nozzle_frame_get_resolved_format(&frame, &resolved) == NOZZLE_ERROR_INVALID_ARGUMENT);

    CHECK(nozzle_frame_get_format_fallback_info(nullptr, &fallback) == NOZZLE_ERROR_INVALID_ARGUMENT);
    CHECK(nozzle_frame_get_format_fallback_info(&frame, nullptr) == NOZZLE_ERROR_INVALID_ARGUMENT);
    CHECK(nozzle_frame_get_format_fallback_info(&frame, &fallback) == NOZZLE_ERROR_INVALID_ARGUMENT);
    CHECK(nozzle_frame_get_format_fallback_info(&writable_frame, &fallback) == NOZZLE_OK);
    CHECK(fallback.requested_format == NOZZLE_FORMAT_UNKNOWN);
    CHECK(fallback.storage_format == NOZZLE_FORMAT_UNKNOWN);
    CHECK(fallback.category == NOZZLE_FALLBACK_CATEGORY_NONE);
}

TEST_CASE("C API: sender and receiver info getters reject null boundaries", "[c_api][info]") {
    NozzleSenderInfo sender_info{};
    NozzleNativeDevice native_device{};
    NozzleConnectedSenderInfo connected_info{};

    CHECK(nozzle_sender_get_info(nullptr, &sender_info) == NOZZLE_ERROR_INVALID_ARGUMENT);
    CHECK(nozzle_sender_get_info(nullptr, nullptr) == NOZZLE_ERROR_INVALID_ARGUMENT);
    CHECK(nozzle_sender_get_native_device(nullptr, &native_device) == NOZZLE_ERROR_INVALID_ARGUMENT);
    CHECK(nozzle_sender_get_native_device(nullptr, nullptr) == NOZZLE_ERROR_INVALID_ARGUMENT);
    CHECK(nozzle_receiver_get_connected_info(nullptr, &connected_info) == NOZZLE_ERROR_INVALID_ARGUMENT);
    CHECK(nozzle_receiver_get_connected_info(nullptr, nullptr) == NOZZLE_ERROR_INVALID_ARGUMENT);
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

TEST_CASE("C API: read-only pixel copy helper preserves row order", "[c_api][pixel_access]") {
    const std::array<uint8_t, 12> source{
        'A', 'B', 'C', '.',
        'D', 'E', 'F', '.',
        'G', 'H', 'I', '.'
    };

    {
        std::array<uint8_t, 9> output{};
        NozzleMappedPixels mapped{};
        REQUIRE(nozzle_test_copy_mapped_pixels_to_buffer(
            source.data(),
            4,
            3,
            3,
            NOZZLE_FORMAT_R8_UNORM,
            NOZZLE_ORIGIN_TOP_LEFT,
            1,
            output.data(),
            output.size(),
            &mapped) == NOZZLE_OK);

        const std::array<uint8_t, 9> expected{
            'A', 'B', 'C',
            'D', 'E', 'F',
            'G', 'H', 'I'
        };
        CHECK(std::memcmp(output.data(), expected.data(), expected.size()) == 0);
        CHECK(mapped.data == output.data());
        CHECK(mapped.row_stride_bytes == 3);
        CHECK(mapped.width == 3);
        CHECK(mapped.height == 3);
        CHECK(mapped.format == NOZZLE_FORMAT_R8_UNORM);
        CHECK(mapped.origin == NOZZLE_ORIGIN_TOP_LEFT);
    }

    {
        std::array<uint8_t, 9> output{};
        NozzleMappedPixels mapped{};
        REQUIRE(nozzle_test_copy_mapped_pixels_to_buffer(
            source.data() + 8,
            -4,
            3,
            3,
            NOZZLE_FORMAT_R8_UNORM,
            NOZZLE_ORIGIN_BOTTOM_LEFT,
            1,
            output.data(),
            output.size(),
            &mapped) == NOZZLE_OK);

        const std::array<uint8_t, 9> expected{
            'G', 'H', 'I',
            'D', 'E', 'F',
            'A', 'B', 'C'
        };
        CHECK(std::memcmp(output.data(), expected.data(), expected.size()) == 0);
        CHECK(mapped.data == output.data());
        CHECK(mapped.row_stride_bytes == 3);
        CHECK(mapped.width == 3);
        CHECK(mapped.height == 3);
        CHECK(mapped.format == NOZZLE_FORMAT_R8_UNORM);
        CHECK(mapped.origin == NOZZLE_ORIGIN_BOTTOM_LEFT);
    }
}

TEST_CASE("C API: read-only pixel copy helper rejects too-small destination", "[c_api][pixel_access]") {
    const std::array<uint8_t, 12> source{
        'A', 'B', 'C', '.',
        'D', 'E', 'F', '.',
        'G', 'H', 'I', '.'
    };
    std::array<uint8_t, 8> output{};
    NozzleMappedPixels mapped{};

    CHECK(nozzle_test_copy_mapped_pixels_to_buffer(
        source.data(),
        4,
        3,
        3,
        NOZZLE_FORMAT_R8_UNORM,
        NOZZLE_ORIGIN_TOP_LEFT,
        1,
        output.data(),
        output.size(),
        &mapped) == NOZZLE_ERROR_INVALID_ARGUMENT);
}

TEST_CASE("C API: read-only pixel copy helper rejects INT64_MIN stride", "[c_api][pixel_access]") {
    const std::array<uint8_t, 1> source{'A'};
    std::array<uint8_t, 1> output{};
    NozzleMappedPixels mapped{};

    CHECK(nozzle_test_copy_mapped_pixels_to_buffer(
        source.data(),
        std::numeric_limits<int64_t>::min(),
        1,
        1,
        NOZZLE_FORMAT_R8_UNORM,
        NOZZLE_ORIGIN_BOTTOM_LEFT,
        1,
        output.data(),
        output.size(),
        &mapped) == NOZZLE_ERROR_INVALID_ARGUMENT);
}

TEST_CASE("C API: sender object wrapper allocation failure cleans registration", "[c_api][allocation]") {
    NozzleSenderDesc probe_desc{};
    probe_desc.name = "c_api_object_alloc_failure_sender_probe";
    probe_desc.application_name = "test";
    probe_desc.ring_buffer_size = 2;

    SenderHandle probe;
    NozzleErrorCode probe_rc = nozzle_sender_create(&probe_desc, &probe.p);
    if (is_backend_unavailable_for_commit_lifecycle(probe_rc)) {
        nozzle_test_clear_c_api_wrapper_object_alloc_failure();
        SKIP("backend device is not available on this runner");
    }
    REQUIRE(probe_rc == NOZZLE_OK);

    NozzleSenderDesc fail_desc{};
    fail_desc.name = "c_api_object_alloc_failure_sender";
    fail_desc.application_name = "test";
    fail_desc.ring_buffer_size = 2;

    NozzleSender *failed_sender =
        reinterpret_cast<NozzleSender *>(static_cast<uintptr_t>(0x1));
    nozzle_test_fail_next_c_api_wrapper_object_alloc();
    CHECK(nozzle_sender_create(&fail_desc, &failed_sender)
        == NOZZLE_ERROR_RESOURCE_CREATION_FAILED);
    CHECK(failed_sender == nullptr);

    SenderHandle retry;
    REQUIRE(nozzle_sender_create(&fail_desc, &retry.p) == NOZZLE_OK);
}

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

    NozzleFrame *raw_probe = nullptr;
    NozzleErrorCode probe_rc = nozzle_sender_acquire_writable_frame(
        sender.p, 4, 4, NOZZLE_FORMAT_RGBA8_UNORM, &raw_probe);
    if (is_backend_unavailable_for_commit_lifecycle(probe_rc)) {
        SKIP("writable frame creation is not available on this runner");
    }
    REQUIRE(probe_rc == NOZZLE_OK);
    FrameHandle probe{raw_probe};
    REQUIRE(nozzle_sender_discard_frame(sender.p, probe.p) == NOZZLE_OK);

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
