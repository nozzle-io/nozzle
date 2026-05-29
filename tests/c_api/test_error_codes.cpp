// nozzle - test_error_codes.cpp - C API error code enum mapping regression tests

#include <catch2/catch_test_macros.hpp>

#include <nozzle/nozzle_c.h>
#include <nozzle/result.hpp>

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
