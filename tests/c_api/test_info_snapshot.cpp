// nozzle - test_info_snapshot.cpp - caller-owned C info snapshot tests

#include <catch2/catch_test_macros.hpp>

#include <nozzle/nozzle_c.h>

TEST_CASE("C API: info snapshot getters reject invalid arguments", "[c_api][info_snapshot]") {
    NozzleSenderInfoSnapshot sender_info{};
    NozzleConnectedSenderSnapshot connected_info{};

    CHECK(nozzle_sender_get_info_snapshot(nullptr, &sender_info)
        == NOZZLE_ERROR_INVALID_ARGUMENT);
    CHECK(nozzle_sender_get_info_snapshot(nullptr, nullptr)
        == NOZZLE_ERROR_INVALID_ARGUMENT);
    CHECK(nozzle_receiver_get_connected_info_snapshot(nullptr, &connected_info)
        == NOZZLE_ERROR_INVALID_ARGUMENT);
    CHECK(nozzle_receiver_get_connected_info_snapshot(nullptr, nullptr)
        == NOZZLE_ERROR_INVALID_ARGUMENT);
}
