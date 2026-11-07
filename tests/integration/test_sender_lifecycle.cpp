#include <nozzle/nozzle.hpp>

#include "ipc.hpp"
#include "registry.hpp"
#include "shared_state.hpp"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstring>
#include <future>
#include <string>
#include <thread>
#include <utility>

namespace {

namespace nd = nozzle::detail;
namespace ipc = nozzle::detail::ipc;
namespace registry = nozzle::detail::registry;

constexpr std::size_t v6_extent = 1664;
constexpr std::size_t v6_padding_begin = 208;
constexpr std::size_t v6_padding_end = 256;
static_assert(sizeof(nd::SenderSharedState) == v6_extent, "v6 sender extent changed");
static_assert(offsetof(nd::SenderSharedState, version) == 4, "v6 version offset changed");
static_assert(offsetof(nd::SenderSharedState, name) == 8, "v6 name offset changed");
static_assert(offsetof(nd::SenderSharedState, uuid) == 136, "v6 UUID offset changed");
static_assert(offsetof(nd::SenderSharedState, width) == 188, "v6 width offset changed");
static_assert(offsetof(nd::SenderSharedState, ring_size) + sizeof(uint32_t) == v6_padding_begin,
              "v6 padding start changed");
static_assert(offsetof(nd::SenderSharedState, committed_frame) == v6_padding_end,
              "v6 committed frame offset changed");
static_assert(offsetof(nd::SenderSharedState, committed_slot) == 320, "v6 slot index offset changed");
static_assert(offsetof(nd::SenderSharedState, slots) == 328, "v6 slots offset changed");
static_assert(offsetof(nd::SenderSharedState, metadata) == 1096, "v6 metadata offset changed");

std::string advertised_state_name(const std::string &id) {
    auto snapshot = registry::read_directory_snapshot();
    INFO(snapshot.error().message);
    REQUIRE(snapshot.ok());
    REQUIRE(snapshot.value().capacity <= snapshot.value().entries.size());
    for(uint32_t i = 0; i < snapshot.value().capacity; ++i) {
        const auto &entry = snapshot.value().entries[i];
        if(entry.valid == nd::entry_valid_flag::valid &&
           std::strncmp(entry.uuid, id.c_str(), sizeof(entry.uuid)) == 0) {
            REQUIRE(std::memchr(entry.shm_name, '\0', sizeof(entry.shm_name)) != nullptr);
            return entry.shm_name;
        }
    }
    FAIL("public sender is not advertised by UUID");
    return {};
}

struct writable_sender_mapping {
    ipc::shm_handle handle{};
    nd::SenderSharedState *state{nullptr};
    uint32_t original_version{0};
    unsigned char original_padding[v6_padding_end - v6_padding_begin]{};

    writable_sender_mapping() = default;
    writable_sender_mapping(const writable_sender_mapping &) = delete;
    writable_sender_mapping &operator=(const writable_sender_mapping &) = delete;

    ~writable_sender_mapping() {
        if(state) {
            // Restore fixture mutations before the real sender's eventual destruction.
            state->version = original_version;
            std::memcpy(reinterpret_cast<unsigned char *>(state) + v6_padding_begin,
                        original_padding, sizeof(original_padding));
            ipc::shm_unmap(state, v6_extent);
        }
        ipc::shm_close(handle);
    }

    void open(const nozzle::sender &sender) {
        REQUIRE(state == nullptr);
        const auto name = advertised_state_name(sender.info().id);
        auto opened = ipc::shm_open_rw(name.c_str());
        INFO(opened.error().message);
        REQUIRE(opened.ok());
        handle = opened.value();
        auto mapped = ipc::shm_map(handle, v6_extent, false);
        INFO(mapped.error().message);
        REQUIRE(mapped.ok());
        state = static_cast<nd::SenderSharedState *>(mapped.value());
        original_version = state->version;
        std::memcpy(original_padding, reinterpret_cast<unsigned char *>(state) + v6_padding_begin,
                    sizeof(original_padding));
        REQUIRE(sender.info().id == state->uuid);
    }

    void emulate_v6_layout() {
        state->version = 6;
        // The old register_sender memset the entire 1664-byte object, including this padding.
        std::memset(reinterpret_cast<unsigned char *>(state) + v6_padding_begin,
                    0, v6_padding_end - v6_padding_begin);
    }

    void stage_closed() {
        ipc::atomic_fence_release();
        ipc::atomic_store_release_32(&state->lifecycle_state, nd::sender_state_closed);
    }
};

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

TEST_CASE("Sender lifecycle: backend-specific version compatibility with actual texture acquisition",
    "[integration][sender_lifecycle][d3d11_sync_version]") {
    const auto desc = unique_sender_desc();
    auto sender = make_sender(desc);
    const auto storage_format = publish_frame(sender, 64, 48);
    writable_sender_mapping mapping;
    mapping.open(sender);

    SECTION("current writer uses D3D11 v7 and preserves other backends v6") {
        const auto expected = sender.info().backend == nozzle::backend_type::d3d11 ? 7u : 6u;
        REQUIRE(mapping.state->version == expected);
    }
    SECTION("emulated v6 zero padding remains readable, not an old-binary test") {
        mapping.emulate_v6_layout();
        nozzle::receiver_desc receiver_desc{};
        receiver_desc.name = desc.name;
        auto created = nozzle::receiver::create(receiver_desc);
        INFO(created.error().message);
        if (sender.info().backend == nozzle::backend_type::d3d11) {
            REQUIRE_FALSE(created.ok());
            REQUIRE(created.error().code == nozzle::ErrorCode::BackendError);
            REQUIRE(created.error().message.find("version") != std::string::npos);
            return;
        }
        REQUIRE(created.ok());
        auto receiver = std::move(created.value());
        require_frame(receiver, 1, 64, 48, storage_format);
        REQUIRE(receiver.connected_info().id == sender.info().id);
    }
}

TEST_CASE("Sender lifecycle: staged teardown excludes an advertised closed sender from lookup",
    "[integration][sender_lifecycle]") {
    const auto desc = unique_sender_desc();
    auto sender = make_sender(desc);
    const auto storage_format = publish_frame(sender, 64, 48);
    writable_sender_mapping mapping;
    mapping.open(sender);
    nozzle::receiver_desc receiver_desc{};
    receiver_desc.name = desc.name;

    // Explicitly stage the teardown interval; real destructor coverage is separate below.
    SECTION("new receiver cannot connect to an advertised closed sender") {
        mapping.stage_closed();
        REQUIRE_FALSE(advertised_state_name(sender.info().id).empty());
        auto created = nozzle::receiver::create(receiver_desc);
        REQUIRE_FALSE(created.ok());
        REQUIRE(created.error().code == nozzle::ErrorCode::SenderNotFound);
    }
    SECTION("connected receiver observes closure then retries lookup as not found") {
        auto created = nozzle::receiver::create(receiver_desc);
        REQUIRE(created.ok());
        auto receiver = std::move(created.value());
        require_frame(receiver, 1, 64, 48, storage_format);
        mapping.stage_closed();
        REQUIRE_FALSE(advertised_state_name(sender.info().id).empty());
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
    }
}

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
