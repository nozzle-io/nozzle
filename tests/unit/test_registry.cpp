#include <catch2/catch_test_macros.hpp>

#include "registry.hpp"

#include <bbb/nozzle/discovery.hpp>
#include <bbb/nozzle/types.hpp>

#include <cstring>
#include <string>
#include <vector>

namespace registry = bbb::nozzle::detail::registry;

static void clear_all_registered_senders() {
    auto senders = bbb::nozzle::enumerate_senders();
    for (const auto &sender : senders) {
        registry::unregister_sender(sender.id.c_str());
    }
}

TEST_CASE("Registry: register and unregister sender", "[registry]") {
    clear_all_registered_senders();

    auto result = registry::register_sender(
        "test_sender", "test_app", 0, 1920, 1080, 1, 3);
    REQUIRE(result.ok());

    auto reg = result.value();

    SECTION("registration fields are valid") {
        REQUIRE(std::strlen(reg.uuid) > 0);
        REQUIRE(reg.uuid[0] != '\0');
        REQUIRE(std::strncmp(reg.shm_name, bbb::nozzle::detail::kSenderShmPrefix, std::strlen(bbb::nozzle::detail::kSenderShmPrefix)) == 0);
    }

    SECTION("open sender state and verify fields") {
        auto state_result = registry::open_sender_state(reg.shm_name);
        REQUIRE(state_result.ok());

        auto view = state_result.value();
        REQUIRE(view.state != nullptr);
        REQUIRE(view.state->width == 1920);
        REQUIRE(view.state->height == 1080);
        REQUIRE(view.state->format == 1);
        REQUIRE(view.state->ring_size == 3);
        REQUIRE(std::strcmp(view.state->name, "test_sender") == 0);
        REQUIRE(std::strcmp(view.state->application_name, "test_app") == 0);

        registry::close_sender_state(view);
        REQUIRE(view.state == nullptr);
    }

    SECTION("unregister succeeds") {
        auto unreg_result = registry::unregister_sender(reg.uuid);
        REQUIRE(unreg_result.ok());
    }

    if (reg.uuid[0] != '\0') {
        registry::unregister_sender(reg.uuid);
    }
}

TEST_CASE("Registry: register multiple senders", "[registry]") {
    clear_all_registered_senders();

    auto r0 = registry::register_sender("sender_alpha", "app", 0, 640, 480, 1, 2);
    auto r1 = registry::register_sender("sender_beta", "app", 0, 1280, 720, 2, 3);
    auto r2 = registry::register_sender("sender_gamma", "app", 0, 1920, 1080, 3, 4);

    REQUIRE(r0.ok());
    REQUIRE(r1.ok());
    REQUIRE(r2.ok());

    auto reg0 = r0.value();
    auto reg1 = r1.value();
    auto reg2 = r2.value();

    REQUIRE(std::strcmp(reg0.uuid, reg1.uuid) != 0);
    REQUIRE(std::strcmp(reg0.uuid, reg2.uuid) != 0);
    REQUIRE(std::strcmp(reg1.uuid, reg2.uuid) != 0);

    REQUIRE(std::strcmp(reg0.shm_name, reg1.shm_name) != 0);
    REQUIRE(std::strcmp(reg0.shm_name, reg2.shm_name) != 0);

    registry::unregister_sender(reg0.uuid);
    registry::unregister_sender(reg1.uuid);
    registry::unregister_sender(reg2.uuid);
}

TEST_CASE("Registry: unregister non-existent sender", "[registry]") {
    auto result = registry::unregister_sender("00000000-0000-0000-0000-000000000000");
    REQUIRE(!result.ok());
    REQUIRE(result.error().code == bbb::nozzle::ErrorCode::SenderNotFound);
}

TEST_CASE("Registry: open sender state", "[registry]") {
    clear_all_registered_senders();

    auto result = registry::register_sender(
        "state_test", "app", 0, 800, 600, 7, 5);
    REQUIRE(result.ok());
    auto reg = result.value();

    auto state_result = registry::open_sender_state(reg.shm_name);
    REQUIRE(state_result.ok());

    auto view = state_result.value();
    REQUIRE(view.state != nullptr);
    REQUIRE(view.state->magic == bbb::nozzle::detail::kSenderMagic);
    REQUIRE(view.state->version == bbb::nozzle::detail::kSharedMemVersion);
    REQUIRE(view.state->width == 800);
    REQUIRE(view.state->height == 600);
    REQUIRE(view.state->format == 7);
    REQUIRE(view.state->ring_size == 5);

    registry::close_sender_state(view);
    REQUIRE(view.state == nullptr);
    REQUIRE(view.mapped == nullptr);
    REQUIRE(view.fd < 0);

    registry::unregister_sender(reg.uuid);
}

TEST_CASE("Registry: open non-existent sender state", "[registry]") {
    auto result = registry::open_sender_state("/nozzle_nonexistent");
    REQUIRE(!result.ok());
}

TEST_CASE("Registry: register with null arguments", "[registry]") {
    auto r1 = registry::register_sender(nullptr, "app", 0, 100, 100, 1, 2);
    REQUIRE(!r1.ok());

    auto r2 = registry::register_sender("name", nullptr, 0, 100, 100, 1, 2);
    REQUIRE(!r2.ok());
}

TEST_CASE("Registry: cleanup stale entries does not crash", "[registry]") {
    registry::cleanup_stale_entries();
    SUCCEED();
}
