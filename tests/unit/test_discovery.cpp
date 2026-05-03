#include <catch2/catch_test_macros.hpp>

#include <nozzle/discovery.hpp>
#include <nozzle/types.hpp>

#include "registry.hpp"

#include <algorithm>
#include <string>
#include <vector>

namespace registry = nozzle::detail::registry;

static void clear_all_registered_senders() {
    auto senders = nozzle::enumerate_senders();
    for (const auto &sender : senders) {
        registry::unregister_sender(sender.id.c_str());
    }
}

static bool contains_sender(
    const std::vector<nozzle::sender_info> &senders,
    const std::string &name)
{
    return std::any_of(senders.begin(), senders.end(),
        [&](const nozzle::sender_info &info) {
            return info.name == name;
        });
}

TEST_CASE("Discovery: empty directory", "[discovery]") {
    clear_all_registered_senders();

    auto senders = nozzle::enumerate_senders();
    REQUIRE(senders.empty());
}

TEST_CASE("Discovery: find registered sender", "[discovery]") {
    clear_all_registered_senders();

    auto reg_result = registry::register_sender(
        "discoverable_sender", "test_app", 0, 640, 480,
        static_cast<uint32_t>(nozzle::texture_format::r8_unorm), 3);
    REQUIRE(reg_result.ok());
    auto reg = reg_result.value();

    SECTION("sender appears in enumeration") {
        auto senders = nozzle::enumerate_senders();
        REQUIRE_FALSE(senders.empty());

        bool found = false;
        for (const auto &s : senders) {
            if (s.id == std::string(reg.uuid)) {
                found = true;
                REQUIRE(s.name == "discoverable_sender");
                REQUIRE(s.application_name == "test_app");
                break;
            }
        }
        REQUIRE(found);
    }

    SECTION("sender disappears after unregister") {
        registry::unregister_sender(reg.uuid);

        auto senders = nozzle::enumerate_senders();
        bool still_present = std::any_of(senders.begin(), senders.end(),
            [&](const nozzle::sender_info &info) {
                return info.id == std::string(reg.uuid);
            });
        REQUIRE_FALSE(still_present);
    }

    if (reg.uuid[0] != '\0') {
        registry::unregister_sender(reg.uuid);
    }
}

TEST_CASE("Discovery: multiple senders", "[discovery]") {
    clear_all_registered_senders();

    auto r0 = registry::register_sender("alpha", "app_a", 0, 320, 240,
        static_cast<uint32_t>(nozzle::texture_format::r8_unorm), 2);
    auto r1 = registry::register_sender("beta", "app_b", 0, 640, 480,
        static_cast<uint32_t>(nozzle::texture_format::rg8_unorm), 3);
    auto r2 = registry::register_sender("gamma", "app_c", 0, 1280, 720,
        static_cast<uint32_t>(nozzle::texture_format::rgba8_unorm), 4);

    REQUIRE(r0.ok());
    REQUIRE(r1.ok());
    REQUIRE(r2.ok());

    auto senders = nozzle::enumerate_senders();
    REQUIRE(senders.size() >= 3);

    REQUIRE(contains_sender(senders, "alpha"));
    REQUIRE(contains_sender(senders, "beta"));
    REQUIRE(contains_sender(senders, "gamma"));

    registry::unregister_sender(r0.value().uuid);
    registry::unregister_sender(r1.value().uuid);
    registry::unregister_sender(r2.value().uuid);
}

TEST_CASE("Discovery: enumerate returns correct backend type", "[discovery]") {
    clear_all_registered_senders();

    auto reg_result = registry::register_sender(
        "backend_test", "app", 1, 100, 100,
        static_cast<uint32_t>(nozzle::texture_format::r8_unorm), 2);
    REQUIRE(reg_result.ok());
    auto reg = reg_result.value();

    auto senders = nozzle::enumerate_senders();
    bool found = false;
    for (const auto &s : senders) {
        if (s.id == std::string(reg.uuid)) {
            found = true;
            REQUIRE(s.backend == nozzle::backend_type::d3d11);
            break;
        }
    }
    REQUIRE(found);

    registry::unregister_sender(reg.uuid);
}
