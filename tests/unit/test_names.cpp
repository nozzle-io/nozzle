#include <catch2/catch_test_macros.hpp>

#include "../name_cases.hpp"
#include "registry.hpp"
#include <nozzle/discovery.hpp>

namespace {

struct registration_guard {
    nozzle::detail::registry::Registration registration;
    ~registration_guard() {
        nozzle::detail::registry::unregister_sender(registration.uuid);
    }
};

} // namespace

TEST_CASE("Registry rejects invalid names without truncating identity", "[names][registry]") {
    for (const auto &name : name_cases::invalid_names()) {
        CAPTURE(name);
        auto result = nozzle::detail::registry::register_sender(name.c_str(), "app", 0, 0, 0, 0, 2);
        if (result.ok()) {
            registration_guard cleanup{result.value()};
            CHECK_FALSE(result.ok());
        } else {
            CHECK(result.error().code == nozzle::ErrorCode::InvalidArgument);
        }
    }
}

TEST_CASE("Registry validates all application bytes before truncation", "[names][registry]") {
    for (const auto &invalid : name_cases::invalid_utf8()) {
        for (const auto &application : {invalid, std::string(64, 'a') + invalid}) {
            CAPTURE(application);
            auto result = nozzle::detail::registry::register_sender("name_invalid_app", application.c_str(), 0, 0, 0, 0, 2);
            if (result.ok()) {
                registration_guard cleanup{result.value()};
                CHECK_FALSE(result.ok());
            } else {
                CHECK(result.error().code == nozzle::ErrorCode::InvalidArgument);
            }
        }
    }
}

TEST_CASE("Registry preserves UTF-8 in directory and sender state", "[names][registry]") {
    for (const auto &sample : name_cases::valid_names()) {
        CAPTURE(sample.name, sample.application);
        auto result = nozzle::detail::registry::register_sender(sample.name.c_str(), sample.application.c_str(), 0, 0, 0, 0, 2);
        REQUIRE(result.ok());
        registration_guard cleanup{result.value()};
        auto view = nozzle::detail::registry::open_sender_state(result.value().shm_name);
        REQUIRE(view.ok());
        const std::string state_name{view.value().state->name};
        const std::string state_application{view.value().state->application_name};
        nozzle::detail::registry::close_sender_state(view.value());
        CHECK(state_name == sample.name);
        CHECK(state_application == sample.stored_application);

        bool found{false};
        for (const auto &info : nozzle::enumerate_senders()) {
            if (info.id != result.value().uuid) continue;
            found = true;
            CHECK(info.name == sample.name);
            CHECK(info.application_name == sample.stored_application);
        }
        CHECK(found);
    }
}
