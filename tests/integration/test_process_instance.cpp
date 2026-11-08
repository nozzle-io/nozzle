#include <nozzle/receiver.hpp>
#include <nozzle/discovery.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cerrno>
#include <cstdio>
#include <cstring>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

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

#if defined(NOZZLE_ENABLE_TEST_HOOKS) && NOZZLE_ENABLE_TEST_HOOKS
namespace {
struct scripted_probe {
    enum class answer { matching, unavailable, closed, different, invalid };
    ipc::pid_type pid{ipc::current_pid()};
    uint64_t token{};
    uint64_t now{10000000000ULL};
    uint64_t queries{0};
    uint32_t native_error{0};
    answer response{answer::matching};

    static nozzle::Result<uint64_t> query(ipc::pid_type pid, void *context) {
        auto &self = *static_cast<scripted_probe *>(context);
        if (pid != self.pid) {
            // Never supply death evidence for unrelated entries during cleanup.
            return nozzle::Error{nozzle::ErrorCode::BackendError, "unrelated owner not scripted"};
        }
        ++self.queries;
        switch (self.response) {
        case answer::matching: return self.token;
        case answer::unavailable: return nozzle::Error{nozzle::ErrorCode::BackendError, "scripted unavailable query"};
        case answer::closed: return nozzle::Error{nozzle::ErrorCode::SenderClosed, "scripted departed owner"};
        case answer::different: return self.token == 1 ? uint64_t{2} : uint64_t{1};
        case answer::invalid: return nozzle::Error{nozzle::ErrorCode::InvalidArgument, "scripted invalid owner"};
        }
        return nozzle::Error{nozzle::ErrorCode::Unknown, "invalid test response"};
    }

    static uint64_t clock(void *context) noexcept {
        return static_cast<scripted_probe *>(context)->now;
    }

    static uint32_t native_probe(ipc::pid_type pid, void *context) noexcept {
        const auto &self = *static_cast<scripted_probe *>(context);
        return pid == self.pid ? self.native_error : 0;
    }

    ipc::process_test_hooks hooks() {
        return {this, query, clock, native_probe};
    }
};

void check_empty_connected(nozzle::receiver &receiver) {
    nozzle::acquire_desc poll{};
    poll.timeout_ms = 0;
    auto result = receiver.acquire_frame(poll);
    CHECK_FALSE(result.ok());
    if (!result.ok()) CHECK(result.error().code == nozzle::ErrorCode::Timeout);
    CHECK(receiver.is_connected());
}

bool sender_visible(const char *uuid) {
    for (const auto &sender : nozzle::enumerate_senders()) {
        if (sender.id == uuid) return true;
    }
    return false;
}
} // namespace

TEST_CASE("Process readers: native permission denial preserves conservative liveness",
          "[integration][process_reader_policy][shared_layout]") {
    const auto pid = ipc::current_pid();
    auto actual = ipc::process_instance_token(pid);
    REQUIRE(actual.ok());
    REQUIRE(actual.value() != 0);
    ipc::process_test_hooks hooks{};
    hooks.native_probe_error = [](ipc::pid_type, void *) noexcept -> uint32_t {
#if defined(_WIN32)
        return ERROR_ACCESS_DENIED;
#else
        return EPERM;
#endif
    };
    {
        ipc::scoped_process_test_hooks scope{hooks};
        CHECK(ipc::is_pid_alive(pid));
        // No token-result callback: POSIX really reads this process's metadata
        // before the injected EPERM from its final kill(pid, 0) probe.
        auto queried = ipc::process_instance_token(pid);
#if defined(_WIN32)
        REQUIRE_FALSE(queried.ok());
        CHECK(queried.error().code == nozzle::ErrorCode::BackendError);
#else
        CHECK(queried.ok());
        if (queried.ok()) CHECK(queried.value() == actual.value());
#endif
    }
    auto restored = ipc::process_instance_token(pid);
    REQUIRE(restored.ok());
    CHECK(restored.value() == actual.value());
}

TEST_CASE("Process readers: unavailable token remains visible connectable and retained",
          "[integration][process_reader_policy][shared_layout]") {
    registered_state owner;
    owner.create();
    REQUIRE(sender_visible(owner.identity.uuid));
    std::array<unsigned char, sizeof(nd::SenderSharedState)> original{};
    std::memcpy(original.data(), owner.state, original.size());
    scripted_probe probe;
    probe.token = owner.original_token;
    probe.response = scripted_probe::answer::unavailable;
#if defined(_WIN32)
    probe.native_error = ERROR_ACCESS_DENIED;
#else
    probe.native_error = EPERM;
#endif
    ipc::scoped_process_test_hooks scope{probe.hooks()};
    CHECK(sender_visible(owner.identity.uuid));
    nozzle::receiver_desc desc{};
    desc.name = owner.state->name;
    auto connected = nozzle::receiver::create(desc);
    CHECK(connected.ok());
    if (connected.ok()) check_empty_connected(connected.value());

    registry::cleanup_stale_entries();
    auto snapshot = registry::read_directory_snapshot();
    REQUIRE(snapshot.ok());
    bool retained = false;
    for (uint32_t i = 0; i < snapshot.value().capacity; ++i) {
        const auto &entry = snapshot.value().entries[i];
        if (entry.valid == nd::entry_valid_flag::valid
            && std::memcmp(entry.uuid, owner.identity.uuid, sizeof(entry.uuid)) == 0) retained = true;
    }
    CHECK(retained);
    auto backing = registry::open_sender_state(owner.identity.shm_name);
    CHECK(backing.ok());
    if (backing.ok()) registry::close_sender_state(backing.value());
    CHECK(std::memcmp(owner.state, original.data(), original.size()) == 0);
    CHECK(probe.queries > 0);
}

TEST_CASE("Process readers: token queries are throttled per receiver including unavailable replies",
          "[integration][process_reader_policy][shared_layout]") {
    registered_state owner;
    owner.create();
    scripted_probe probe;
    probe.token = owner.original_token;
    ipc::scoped_process_test_hooks scope{probe.hooks()};
    nozzle::receiver_desc desc{};
    desc.name = owner.state->name;
    auto first = nozzle::receiver::create(desc);
    auto second = nozzle::receiver::create(desc);
    REQUIRE(first.ok());
    REQUIRE(second.ok());
    REQUIRE(probe.queries >= 2);
    probe.queries = 0;
    SECTION("matching tokens") {}
    SECTION("unavailable replies consume the same retry budget") {
        probe.response = scripted_probe::answer::unavailable;
    }

    // Fixed calling-thread clock; all polls are zero-timeout, never waits.
    for (unsigned i = 0; i < 8; ++i) {
        check_empty_connected(first.value());
        check_empty_connected(second.value());
    }
    CHECK(probe.queries == 0);
    probe.now += 999999999ULL;
    check_empty_connected(first.value());
    check_empty_connected(second.value());
    CHECK(probe.queries == 0);
    ++probe.now;
    check_empty_connected(first.value());
    CHECK(probe.queries == 1);
    check_empty_connected(first.value());
    CHECK(probe.queries == 1);
    check_empty_connected(second.value());
    CHECK(probe.queries == 2);
    for (unsigned i = 0; i < 8; ++i) check_empty_connected(second.value());
    CHECK(probe.queries == 2);
    probe.now += 1000000000ULL;
    check_empty_connected(first.value());
    CHECK(probe.queries == 3);
    check_empty_connected(second.value());
    CHECK(probe.queries == 4);
}

TEST_CASE("Process readers: positive owner evidence still rejects while unknown alone does not",
          "[integration][process_reader_policy][shared_layout]") {
    registered_state owner;
    owner.create();
    scripted_probe probe;
    probe.token = owner.original_token;
    ipc::scoped_process_test_hooks scope{probe.hooks()};
    nozzle::receiver_desc desc{};
    desc.name = owner.state->name;
    auto connected = nozzle::receiver::create(desc);
    REQUIRE(connected.ok());
    probe.queries = 0;
    nozzle::ErrorCode expected = nozzle::ErrorCode::SenderClosed;
    bool cheap_probe = false;
    SECTION("different token after refresh deadline") {
        probe.response = scripted_probe::answer::different;
        probe.now += 1000000000ULL;
    }
    SECTION("departed token after refresh deadline") {
        probe.response = scripted_probe::answer::closed;
        probe.now += 1000000000ULL;
    }
    SECTION("invalid query is not reader fail-open") {
        probe.response = scripted_probe::answer::invalid;
        probe.now += 1000000000ULL;
        expected = nozzle::ErrorCode::InvalidArgument;
    }
    SECTION("cheap liveness detects departure before refresh deadline") {
        cheap_probe = true;
#if defined(_WIN32)
        probe.native_error = ERROR_INVALID_PARAMETER;
#else
        probe.native_error = ESRCH;
#endif
    }
    nozzle::acquire_desc poll{};
    poll.timeout_ms = 0;
    auto result = connected.value().acquire_frame(poll);
    REQUIRE_FALSE(result.ok());
    CHECK(result.error().code == expected);
    if (expected == nozzle::ErrorCode::SenderClosed) CHECK_FALSE(connected.value().is_connected());
    CHECK(probe.queries == (cheap_probe ? 0 : 1));
    if (!cheap_probe) {
        CHECK_FALSE(sender_visible(owner.identity.uuid));
        auto fresh = nozzle::receiver::create(desc);
        REQUIRE_FALSE(fresh.ok());
        CHECK(fresh.error().code == (expected == nozzle::ErrorCode::InvalidArgument
            ? nozzle::ErrorCode::InvalidArgument : nozzle::ErrorCode::SenderNotFound));
    }
}
#endif
