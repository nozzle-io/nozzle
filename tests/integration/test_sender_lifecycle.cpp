#include <nozzle/nozzle.hpp>

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <future>
#include <string>
#include <thread>
#include <utility>

namespace {

nozzle::sender make_sender(const nozzle::sender_desc &desc) {
#if NOZZLE_PLATFORM_MACOS
    REQUIRE(NOZZLE_HAS_METAL == 1);
    constexpr auto expected_backend = nozzle::backend_type::metal;
#elif NOZZLE_PLATFORM_WINDOWS
    REQUIRE(NOZZLE_HAS_D3D11 == 1);
    constexpr auto expected_backend = nozzle::backend_type::d3d11;
#else
    SKIP("Sender lifecycle requires the default Metal/D3D11 texture backend; unavailable on Linux");
    constexpr auto expected_backend = nozzle::backend_type::unknown;
#endif
    REQUIRE(desc.native_device.device == nullptr);
    auto result = nozzle::sender::create(desc);
    INFO(result.error().message);
    REQUIRE(result.ok());
    REQUIRE(result.value().native_device().device != nullptr);
    REQUIRE(result.value().native_device().backend == expected_backend);
    REQUIRE(result.value().info().backend == expected_backend);
    // Explicit publication keeps this lifecycle test independent of initial metadata setup.
    auto metadata = result.value().set_metadata(desc.metadata);
    INFO(metadata.error().message);
    REQUIRE(metadata.ok());
    return std::move(result.value());
}

nozzle::sender_desc unique_sender_desc() {
    nozzle::sender_desc desc{};
    desc.name = "lifecycle_" + std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count());
    desc.application_name = "lifecycle_original";
    desc.metadata = {{"generation", "original"}};
    return desc;
}

nozzle::texture_format publish_frame(nozzle::sender &sender, uint32_t width, uint32_t height) {
    nozzle::texture_desc desc{};
    desc.width = width;
    desc.height = height;
    desc.format = nozzle::texture_format::rgba8_unorm;
    auto writable = sender.acquire_writable_frame(desc);
    INFO(writable.error().message);
    REQUIRE(writable.ok());
    REQUIRE(writable.value().get_texture().valid());
    const auto storage_format = writable.value().get_texture().desc().format;
    auto committed = sender.commit_frame(writable.value());
    INFO(committed.error().message);
    REQUIRE(committed.ok());
    return storage_format;
}

void require_frame(nozzle::receiver &receiver, uint64_t index, uint32_t width, uint32_t height,
                   nozzle::texture_format storage_format) {
    nozzle::acquire_desc desc{};
    desc.timeout_ms = 1000;
    auto result = receiver.acquire_frame(desc);
    INFO(result.error().message);
    REQUIRE(result.ok());
    REQUIRE(result.value().valid());
    REQUIRE(result.value().get_texture().valid());
    const auto info = result.value().info();
    REQUIRE(info.frame_index == index);
    REQUIRE(info.width == width);
    REQUIRE(info.height == height);
    REQUIRE(info.format == storage_format);
    REQUIRE(info.dropped_frame_count == 0);
    // Release backend synchronization before destroying or publishing a sender.
    result.value().release();
}

} // namespace

TEST_CASE("Sender lifecycle: live-process destruction and same-name reconnect",
    "[integration][sender_lifecycle]") {
    auto desc = unique_sender_desc();
    auto sender = make_sender(desc);
    const auto original_format = publish_frame(sender, 64, 48);
    nozzle::receiver_desc receiver_desc{};
    receiver_desc.name = desc.name;
    auto created = nozzle::receiver::create(receiver_desc);
    REQUIRE(created.ok());
    auto receiver = std::move(created.value());
    require_frame(receiver, 1, 64, 48, original_format);
    const auto original = receiver.connected_info();
    REQUIRE(original.id == sender.info().id);
    REQUIRE_FALSE(original.id.empty());
    REQUIRE(original.width == 64);
    REQUIRE(original.height == 48);
    const auto original_metadata = receiver.sender_metadata();
    REQUIRE(original_metadata.size() == 1);
    REQUIRE(original_metadata[0].value == "original");

    sender = nozzle::sender{};
    nozzle::acquire_desc acquire{};
    acquire.timeout_ms = 250;
    auto closed = receiver.acquire_frame(acquire);
    REQUIRE_FALSE(closed.ok());
    REQUIRE(closed.error().code == nozzle::ErrorCode::SenderClosed);
    REQUIRE_FALSE(receiver.is_connected());
    auto absent = receiver.acquire_frame(acquire);
    REQUIRE_FALSE(absent.ok());
    REQUIRE(absent.error().code == nozzle::ErrorCode::SenderNotFound);
    REQUIRE_FALSE(receiver.is_connected());

    desc.application_name = "lifecycle_replacement";
    desc.metadata = {{"generation", "replacement"}};
    sender = make_sender(desc);
    REQUIRE(sender.info().id != original.id);
    const auto replacement_format = publish_frame(sender, 96, 80);
    require_frame(receiver, 1, 96, 80, replacement_format);
    REQUIRE(receiver.is_connected());
    const auto replacement = receiver.connected_info();
    REQUIRE(replacement.id == sender.info().id);
    REQUIRE(replacement.id != original.id);
    REQUIRE(replacement.name == desc.name);
    REQUIRE(replacement.application_name == desc.application_name);
    REQUIRE(replacement.backend == original.backend);
    REQUIRE(replacement.width == 96);
    REQUIRE(replacement.height == 80);
    REQUIRE(replacement.format == replacement_format);
    REQUIRE(replacement.frame_counter == 1);
    const auto metadata = receiver.sender_metadata();
    REQUIRE(metadata.size() == 1);
    REQUIRE(metadata[0].key == "generation");
    REQUIRE(metadata[0].value == "replacement");
}

TEST_CASE("Sender lifecycle: closure interrupts frame acquisition",
    "[integration][sender_lifecycle]") {
    const auto desc = unique_sender_desc();
    auto sender = make_sender(desc);
    nozzle::receiver_desc receiver_desc{};
    receiver_desc.name = desc.name;
    auto created = nozzle::receiver::create(receiver_desc);
    REQUIRE(created.ok());
    auto receiver = std::move(created.value());

    SECTION("awaiting first frame") {}
    SECTION("awaiting new frame") {
        const auto storage_format = publish_frame(sender, 64, 48);
        require_frame(receiver, 1, 64, 48, storage_format);
    }

    nozzle::acquire_desc acquire{};
    acquire.timeout_ms = 2000;
    std::promise<void> started;
    auto ready = started.get_future();
    std::atomic<bool> finished{false};
    nozzle::Result<nozzle::frame> result{nozzle::Error{
        nozzle::ErrorCode::Unknown, "worker has not acquired"}};
    std::thread worker([&] {
        started.set_value();
        result = receiver.acquire_frame(acquire);
        finished.store(true);
    });
    const bool entered = ready.wait_for(std::chrono::seconds(2)) == std::future_status::ready;
    // Public API has no wait-entry hook; allow acquisition to enter its retry loop.
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    const bool pending = !finished.load();
    sender = nozzle::sender{};
    // No fatal assertions while the worker is joinable. Acquisition has a deadline.
    worker.join();

    REQUIRE(entered);
    REQUIRE(pending);
    INFO(result.error().message);
    REQUIRE_FALSE(result.ok());
    REQUIRE(result.error().code == nozzle::ErrorCode::SenderClosed);
    REQUIRE_FALSE(receiver.is_connected());
    auto absent = receiver.acquire_frame(acquire);
    REQUIRE_FALSE(absent.ok());
    REQUIRE(absent.error().code == nozzle::ErrorCode::SenderNotFound);
    REQUIRE_FALSE(receiver.is_connected());
}
