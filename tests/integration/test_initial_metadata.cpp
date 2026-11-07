#include <nozzle/nozzle.hpp>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstddef>
#include <string>
#include <utility>

namespace {

void require_metadata(const nozzle::metadata_list &actual, const nozzle::metadata_list &expected) {
    REQUIRE(actual.size() == expected.size());
    for(std::size_t i = 0; i < expected.size(); ++i) {
        CAPTURE(i);
        REQUIRE(actual[i].key == expected[i].key);
        REQUIRE(actual[i].value == expected[i].value);
    }
}

} // namespace

TEST_CASE("Initial metadata: descriptor is visible before any update or frame",
    "[integration][initial_metadata]") {
#if NOZZLE_PLATFORM_MACOS
    REQUIRE(NOZZLE_HAS_METAL == 1);
    constexpr auto expected_backend = nozzle::backend_type::metal;
#elif NOZZLE_PLATFORM_WINDOWS
    REQUIRE(NOZZLE_HAS_D3D11 == 1);
    constexpr auto expected_backend = nozzle::backend_type::d3d11;
#else
    SKIP("Initial metadata requires the default Metal/D3D11 device; unavailable on Linux");
    constexpr auto expected_backend = nozzle::backend_type::unknown;
#endif
    nozzle::sender_desc desc{};
    desc.name = "initial_metadata_" + std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count());
    desc.application_name = "initial_metadata_test";
    SECTION("empty descriptor metadata control") {}
    SECTION("multiple descriptor entries including equals and empty values") {
        desc.metadata = {
            {"color_space", "linear"},
            {"channel_semantics", "depth"},
            {"expression", "left=right"},
            {"label", ""},
        };
    }

    REQUIRE(desc.native_device.device == nullptr);
    auto created_sender = nozzle::sender::create(desc);
    INFO(created_sender.error().message);
    REQUIRE(created_sender.ok());
    auto sender = std::move(created_sender.value());
    REQUIRE(sender.native_device().device != nullptr);
    REQUIRE(sender.native_device().backend == expected_backend);
    REQUIRE(sender.info().backend == expected_backend);

    nozzle::receiver_desc receiver_desc{};
    receiver_desc.name = desc.name;
    auto created_receiver = nozzle::receiver::create(receiver_desc);
    INFO(created_receiver.error().message);
    REQUIRE(created_receiver.ok());
    auto receiver = std::move(created_receiver.value());
    REQUIRE(receiver.connected_info().id == sender.info().id);
    require_metadata(receiver.sender_metadata(), desc.metadata);

    // Reached only after the descriptor-only read passes; no initial set_metadata workaround.
    const nozzle::metadata_list replacement{{"color_space", "srgb"}, {"generation", "updated"}};
    auto updated = sender.set_metadata(replacement);
    INFO(updated.error().message);
    REQUIRE(updated.ok());
    require_metadata(receiver.sender_metadata(), replacement);
}
