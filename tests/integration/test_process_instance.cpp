#include <nozzle/receiver.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstdio>

#include "ipc.hpp"
#include "registry.hpp"

namespace {
namespace nd = nozzle::detail;
namespace ipc = nozzle::detail::ipc;
namespace registry = nozzle::detail::registry;

struct registered_state {
    registry::Registration identity{};
    ipc::shm_handle handle{};
    nd::SenderSharedState *state{nullptr};
    uint64_t original_token{};
    uint32_t original_version{};

    ~registered_state() {
        if (state) {
            auto locked = ipc::dir_lock_acquire(nd::kDirectoryShmName, true);
            if (locked.ok()) {
                state->owner_instance = original_token;
                state->version = original_version;
                ipc::dir_lock_release(locked.value());
            } else {
                std::fprintf(stderr, "instance fixture restore lock failed\n");
            }
            ipc::shm_unmap(state, sizeof(*state));
        }
        ipc::shm_close(handle);
        if (identity.uuid[0]) {
            const auto removed = registry::unregister_sender(identity.uuid);
            if (!removed.ok()) std::fprintf(stderr, "instance fixture cleanup: %s\n", removed.error().message.c_str());
        }
    }

    void create(const char *requested_name = nullptr) {
        uint64_t nonce{};
        ipc::random_bytes(&nonce, sizeof(nonce));
        char name[64]{};
        std::snprintf(name, sizeof(name), "instance_%llu_%016llx",
            static_cast<unsigned long long>(ipc::current_pid()),
            static_cast<unsigned long long>(nonce));
        auto registered = registry::register_sender(requested_name ? requested_name : name,
                                                   "instance_tests", 1, 4, 3, 0, 1);
        REQUIRE(registered.ok());
        identity = registered.value();
        auto opened = ipc::shm_open_rw(identity.shm_name);
        REQUIRE(opened.ok());
        handle = opened.value();
        auto mapped = ipc::shm_map(handle, sizeof(nd::SenderSharedState), false);
        REQUIRE(mapped.ok());
        state = static_cast<nd::SenderSharedState *>(mapped.value());
        original_token = state->owner_instance;
        original_version = state->version;
        REQUIRE(original_token != 0);
        REQUIRE(state->owner_pid == ipc::current_pid());
    }

    bool set_token(uint64_t token) {
        auto locked = ipc::dir_lock_acquire(nd::kDirectoryShmName, true);
        if (!locked.ok()) return false;
        state->owner_instance = token;
        ipc::dir_lock_release(locked.value());
        return true;
    }
};
} // namespace

TEST_CASE("Process instance: changed stored identity disconnects an established receiver",
          "[integration][process_instance][shared_layout]") {
    // Real registry and public receiver, but no published GPU resource. This
    // exercises identity/lifecycle logic, not actual PID reuse or GPU transport.
    registered_state owner;
    owner.create();
    nozzle::receiver_desc desc{};
    desc.name = owner.state->name;
    auto connected = nozzle::receiver::create(desc);
    INFO(connected.error().message);
    REQUIRE(connected.ok());
    auto &receiver = connected.value();
    REQUIRE(receiver.is_connected());
    nozzle::acquire_desc poll{};
    poll.timeout_ms = 0;
    auto empty = receiver.acquire_frame(poll);
    REQUIRE_FALSE(empty.ok());
    REQUIRE(empty.error().code == nozzle::ErrorCode::Timeout);
    REQUIRE(receiver.is_connected());

    REQUIRE(owner.set_token(owner.original_token == 1 ? 2 : 1));
    auto ended = receiver.acquire_frame(poll);
    REQUIRE_FALSE(ended.ok());
    CHECK(ended.error().code == nozzle::ErrorCode::SenderClosed);
    CHECK_FALSE(receiver.is_connected());
    auto fresh = nozzle::receiver::create(desc);
    REQUIRE_FALSE(fresh.ok());
    CHECK(fresh.error().code == nozzle::ErrorCode::SenderNotFound);
    auto retry = receiver.acquire_frame(poll);
    REQUIRE_FALSE(retry.ok());
    CHECK(retry.error().code == nozzle::ErrorCode::SenderNotFound);
    CHECK_FALSE(receiver.is_connected());
}

TEST_CASE("Process instance: unusable same-name entries do not hide a healthy replacement",
          "[integration][process_instance][shared_layout]") {
    registered_state earlier;
    earlier.create();
    registered_state healthy;
    healthy.create(earlier.state->name);
    SECTION("older sender layout precedes replacement") {
        auto lock = ipc::dir_lock_acquire(nd::kDirectoryShmName, true);
        REQUIRE(lock.ok());
        earlier.state->version = 7;
        ipc::dir_lock_release(lock.value());
    }
    SECTION("missing backing precedes replacement") {
        auto lock = ipc::dir_lock_acquire(nd::kDirectoryShmName, true);
        REQUIRE(lock.ok());
        ipc::shm_unlink(earlier.identity.shm_name);
        ipc::dir_lock_release(lock.value());
#if defined(_WIN32)
        // Windows keeps the name while the fixture view/handle exists.
        ipc::shm_unmap(earlier.state, sizeof(*earlier.state));
        earlier.state = nullptr;
        ipc::shm_close(earlier.handle);
#endif
    }
    nozzle::receiver_desc desc{};
    desc.name = healthy.state->name;
    auto connected = nozzle::receiver::create(desc);
    INFO(connected.error().message);
    REQUIRE(connected.ok());
    CHECK(connected.value().connected_info().id == healthy.identity.uuid);
}
