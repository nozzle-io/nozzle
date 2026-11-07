#include <catch2/catch_test_macros.hpp>

#include "../name_cases.hpp"
#include <nozzle/nozzle_c.h>
#include <nozzle/receiver.hpp>
#include <nozzle/sender.hpp>

#include <memory>

TEST_CASE("C and C++ APIs reject invalid sender and receiver identities", "[names][name_validation]") {
    for (const auto &name : name_cases::invalid_names()) {
        CAPTURE(name);
        nozzle::sender_desc sender_desc{};
        sender_desc.name = name;
        auto sender = nozzle::sender::create(sender_desc);
        CHECK_FALSE(sender.ok());
        if (!sender.ok()) CHECK(sender.error().code == nozzle::ErrorCode::InvalidArgument);

        nozzle::receiver_desc receiver_desc{};
        receiver_desc.name = name;
        auto receiver = nozzle::receiver::create(receiver_desc);
        CHECK_FALSE(receiver.ok());
        if (!receiver.ok()) CHECK(receiver.error().code == nozzle::ErrorCode::InvalidArgument);

        NozzleSenderDesc c_sender_desc{};
        c_sender_desc.name = name.c_str();
        NozzleSender *c_sender{nullptr};
        const auto sender_error = nozzle_sender_create(&c_sender_desc, &c_sender);
        std::unique_ptr<NozzleSender, decltype(&nozzle_sender_destroy)> sender_cleanup{c_sender, nozzle_sender_destroy};
        CHECK(sender_error == NOZZLE_ERROR_INVALID_ARGUMENT);
        CHECK(c_sender == nullptr);

        NozzleReceiverDesc c_receiver_desc{};
        c_receiver_desc.name = name.c_str();
        NozzleReceiver *c_receiver{nullptr};
        const auto receiver_error = nozzle_receiver_create(&c_receiver_desc, &c_receiver);
        std::unique_ptr<NozzleReceiver, decltype(&nozzle_receiver_destroy)> receiver_cleanup{c_receiver, nozzle_receiver_destroy};
        CHECK(receiver_error == NOZZLE_ERROR_INVALID_ARGUMENT);
        CHECK(c_receiver == nullptr);
    }
}

TEST_CASE("C and C++ APIs validate entire application name", "[names][name_validation]") {
    for (const auto &invalid : name_cases::invalid_utf8()) {
        for (const auto &application : {invalid, std::string(64, 'a') + invalid}) {
            CAPTURE(application);
            nozzle::sender_desc desc{};
            desc.name = "api_invalid_app";
            desc.application_name = application;
            auto sender = nozzle::sender::create(desc);
            CHECK_FALSE(sender.ok());
            if (!sender.ok()) CHECK(sender.error().code == nozzle::ErrorCode::InvalidArgument);

            NozzleSenderDesc c_desc{};
            c_desc.name = desc.name.c_str();
            c_desc.application_name = application.c_str();
            NozzleSender *c_sender{nullptr};
            const auto error = nozzle_sender_create(&c_desc, &c_sender);
            std::unique_ptr<NozzleSender, decltype(&nozzle_sender_destroy)> cleanup{c_sender, nozzle_sender_destroy};
            CHECK(error == NOZZLE_ERROR_INVALID_ARGUMENT);
            CHECK(c_sender == nullptr);
        }
    }
}

TEST_CASE("C++ rejects embedded NUL rather than silently aliasing names", "[names][name_validation]") {
    const std::string embedded{"prefix\0suffix", 13};
    nozzle::sender_desc desc{};
    desc.name = embedded;
    auto sender = nozzle::sender::create(desc);
    CHECK_FALSE(sender.ok());
    if (!sender.ok()) CHECK(sender.error().code == nozzle::ErrorCode::InvalidArgument);
    nozzle::receiver_desc receiver_desc{};
    receiver_desc.name = embedded;
    auto receiver = nozzle::receiver::create(receiver_desc);
    CHECK_FALSE(receiver.ok());
    if (!receiver.ok()) CHECK(receiver.error().code == nozzle::ErrorCode::InvalidArgument);
    desc.name = "embedded_app";
    desc.application_name = embedded;
    auto app_sender = nozzle::sender::create(desc);
    CHECK_FALSE(app_sender.ok());
    if (!app_sender.ok()) CHECK(app_sender.error().code == nozzle::ErrorCode::InvalidArgument);
}
