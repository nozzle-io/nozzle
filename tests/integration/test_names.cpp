#include <catch2/catch_test_macros.hpp>

#include "../name_cases.hpp"
#include <nozzle/nozzle_c.h>
#include <nozzle/receiver.hpp>
#include <nozzle/sender.hpp>

#include <memory>

TEST_CASE("Name boundaries round trip through C++ and C snapshots", "[names][name_roundtrip]") {
    for (const auto &sample : name_cases::valid_names()) {
        CAPTURE(sample.name, sample.application);
        nozzle::sender_desc desc{};
        desc.name = sample.name;
        desc.application_name = sample.application;
        {
            auto sender = nozzle::sender::create(desc);
#if NOZZLE_HAS_DMA_BUF
            if (!sender.ok() && sender.error().code == nozzle::ErrorCode::BackendError) {
                SKIP("Linux backend device unavailable; name round trip not exercised");
            }
#endif
            REQUIRE(sender.ok());
            CHECK(sender.value().info().name == sample.name);
            CHECK(sender.value().info().application_name == sample.stored_application);
            nozzle::receiver_desc receiver_desc{};
            receiver_desc.name = sample.name;
            auto receiver = nozzle::receiver::create(receiver_desc);
            REQUIRE(receiver.ok());
            CHECK(receiver.value().connected_info().name == sample.name);
            CHECK(receiver.value().connected_info().application_name == sample.stored_application);
        }

        NozzleSenderDesc c_desc{};
        c_desc.name = sample.name.c_str();
        c_desc.application_name = sample.application.c_str();
        NozzleSender *sender{nullptr};
        const auto create_error = nozzle_sender_create(&c_desc, &sender);
        std::unique_ptr<NozzleSender, decltype(&nozzle_sender_destroy)> sender_cleanup{sender, nozzle_sender_destroy};
        REQUIRE(create_error == NOZZLE_OK);
        NozzleSenderInfoSnapshot sender_info{};
        REQUIRE(nozzle_sender_get_info_snapshot(sender, &sender_info) == NOZZLE_OK);
        CHECK(std::string{sender_info.name} == sample.name);
        CHECK(std::string{sender_info.application_name} == sample.stored_application);

        NozzleReceiverDesc receiver_desc{};
        receiver_desc.name = sample.name.c_str();
        NozzleReceiver *receiver{nullptr};
        const auto receiver_error = nozzle_receiver_create(&receiver_desc, &receiver);
        std::unique_ptr<NozzleReceiver, decltype(&nozzle_receiver_destroy)> receiver_cleanup{receiver, nozzle_receiver_destroy};
        REQUIRE(receiver_error == NOZZLE_OK);
        NozzleConnectedSenderSnapshot receiver_info{};
        REQUIRE(nozzle_receiver_get_connected_info_snapshot(receiver, &receiver_info) == NOZZLE_OK);
        CHECK(std::string{receiver_info.name} == sample.name);
        CHECK(std::string{receiver_info.application_name} == sample.stored_application);
        CHECK(receiver_info.connected == 1);
    }
}
