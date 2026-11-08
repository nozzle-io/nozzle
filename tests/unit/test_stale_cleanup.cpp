#include <catch2/catch_test_macros.hpp>

#if defined(_WIN32)

TEST_CASE("Stale cleanup: POSIX owner crash regression", "[registry][stale_cleanup]") {
    SKIP("POSIX shm_unlink and reaped process ownership; Windows uses handle lifetime");
}

#else

#include <array>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "ipc.hpp"
#include "registry.hpp"
#include "shared_layout.hpp"

extern char **environ;

namespace {
namespace nd = nozzle::detail;
namespace ipc = nozzle::detail::ipc;
namespace registry = nozzle::detail::registry;
using clock_type = std::chrono::steady_clock;

std::string unique_name() {
    uint64_t nonce{};
    ipc::random_bytes(&nonce, sizeof(nonce));
    char name[64]{};
    std::snprintf(name, sizeof(name), "stale_%llu_%016llx",
        static_cast<unsigned long long>(ipc::current_pid()),
        static_cast<unsigned long long>(nonce));
    return name;
}

struct directory_access {
    ipc::dir_lock lock{};
    ipc::shm_handle handle{};
    void *mapped{nullptr};
    static constexpr std::size_t size = sizeof(nd::DirectoryHeader)
        + sizeof(nd::DirectoryEntry) * nd::kMaxSenders;

    ~directory_access() {
        if (mapped) ipc::shm_unmap(mapped, size);
        ipc::shm_close(handle);
        ipc::dir_lock_release(lock);
    }

    bool open() {
        auto locked = ipc::dir_lock_acquire(nd::kDirectoryShmName, true);
        if (!locked.ok()) return false;
        lock = locked.value();
        auto opened = ipc::shm_open_rw(nd::kDirectoryShmName);
        if (!opened.ok()) return false;
        handle = opened.value();
        auto view = ipc::shm_map(handle, size, false);
        if (!view.ok()) return false;
        mapped = view.value();
        return nd::validate_directory_header(*static_cast<nd::DirectoryHeader *>(mapped)).ok();
    }

    nd::DirectoryEntry *find(const char *uuid) {
        auto *header = static_cast<nd::DirectoryHeader *>(mapped);
        auto *entries = reinterpret_cast<nd::DirectoryEntry *>(header + 1);
        for (uint32_t i = 0; i < header->capacity; ++i) {
            if (std::memcmp(entries[i].uuid, uuid, 37) == 0) return &entries[i];
        }
        return nullptr;
    }
};

struct owned_sender {
    registry::Registration identity{};
    registry::SenderStateView view{};

    ~owned_sender() {
        registry::close_sender_state(view);
        if (!identity.uuid[0]) return;
        // A sweep may already have invalidated/reused this slot. Never restore
        // a whole directory or unregister a possibly substituted shm name.
        directory_access directory;
        if (directory.open()) {
            auto *entry = directory.find(identity.uuid);
            if (entry && std::memcmp(entry->shm_name, identity.shm_name,
                                    sizeof(identity.shm_name)) == 0) {
                entry->valid = nd::entry_valid_flag::invalid;
            }
            ipc::shm_unlink(identity.shm_name);
        }
    }

    void create() {
        const auto name = unique_name();
        auto registered = registry::register_sender(
            name.c_str(), "stale_cleanup_tests", 1, 4, 3, 0, 1);
        REQUIRE(registered.ok());
        identity = registered.value();
        retain_view();
    }

    void retain_view() {
        auto opened = registry::open_sender_state(identity.shm_name);
        REQUIRE(opened.ok());
        view = opened.value();
    }
};

struct child_owner {
    owned_sender sender;
    std::string name{unique_name()};
    pid_t pid{-1};
    int input[2]{-1, -1};
    int output[2]{-1, -1};
    bool reaped{false};

    static void close_fd(int &fd) {
        if (fd >= 0) ::close(fd);
        fd = -1;
    }

    bool kill_and_reap() {
        if (pid <= 0 || reaped) return false;
        if (::kill(pid, SIGKILL) != 0 && errno != ESRCH) return false;
        const auto deadline = clock_type::now() + std::chrono::seconds(5);
        while (clock_type::now() < deadline) {
            int status{};
            const pid_t result = ::waitpid(pid, &status, WNOHANG);
            if (result == pid) {
                reaped = true;
                return WIFSIGNALED(status) && WTERMSIG(status) == SIGKILL;
            }
            if (result < 0 && errno != EINTR) return false;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        return false;
    }

    ~child_owner() {
        if (pid > 0 && !reaped && !kill_and_reap()) {
            std::fprintf(stderr, "stale cleanup helper could not be reaped: %ld\n",
                         static_cast<long>(pid));
        }
        for (auto &fd : input) close_fd(fd);
        for (auto &fd : output) close_fd(fd);
        // Also clean a registration if readiness parsing failed after creation.
        if (pid > 0 && reaped && !sender.identity.uuid[0]) {
            auto snapshot = registry::read_directory_snapshot();
            if (snapshot.ok()) {
                for (uint32_t i = 0; i < snapshot.value().capacity; ++i) {
                    const auto &entry = snapshot.value().entries[i];
                    if (nd::valid_directory_strings(entry) && entry.pid == static_cast<uint64_t>(pid)
                        && name == entry.sender_name) {
                        std::memcpy(sender.identity.uuid, entry.uuid, sizeof(entry.uuid));
                        std::memcpy(sender.identity.shm_name, entry.shm_name, sizeof(entry.shm_name));
                        break;
                    }
                }
            }
        }
    }

    void start() {
        REQUIRE(::pipe(input) == 0);
        REQUIRE(::pipe(output) == 0);
        for (const int fd : {input[0], input[1], output[0], output[1]}) {
            REQUIRE(::fcntl(fd, F_SETFD, FD_CLOEXEC) == 0);
        }
        posix_spawn_file_actions_t actions;
        REQUIRE(::posix_spawn_file_actions_init(&actions) == 0);
        int error = ::posix_spawn_file_actions_adddup2(&actions, input[0], STDIN_FILENO);
        if (!error) error = ::posix_spawn_file_actions_adddup2(&actions, output[1], STDOUT_FILENO);
        char *args[]{const_cast<char *>(NOZZLE_STALE_SENDER_OWNER_PATH), name.data(), nullptr};
        if (!error) error = ::posix_spawn(&pid, args[0], &actions, nullptr, args, environ);
        ::posix_spawn_file_actions_destroy(&actions);
        REQUIRE(error == 0);
        close_fd(input[0]);
        close_fd(output[1]);

        std::string line;
        const auto deadline = clock_type::now() + std::chrono::seconds(5);
        while (line.size() < 160 && clock_type::now() < deadline) {
            pollfd ready{output[0], POLLIN, 0};
            const int result = ::poll(&ready, 1, 50);
            if (result < 0 && errno == EINTR) continue;
            REQUIRE(result >= 0);
            if (!result) continue;
            char byte{};
            REQUIRE(::read(output[0], &byte, 1) == 1);
            line += byte;
            if (byte == '\n') break;
        }
        REQUIRE_FALSE(line.empty());
        REQUIRE(line.back() == '\n');
        registry::Registration identity{};
        char extra{};
        REQUIRE(std::sscanf(line.c_str(), "%36s %95s %c", identity.uuid,
                            identity.shm_name, &extra) == 2);
        REQUIRE(std::strlen(identity.uuid) == 36);
        char canonical[96]{};
        std::snprintf(canonical, sizeof(canonical), "%s%.8s", nd::kSenderShmPrefix, identity.uuid);
        REQUIRE(std::strcmp(identity.shm_name, canonical) == 0);
        sender.identity = identity;
        sender.retain_view();
        REQUIRE(std::strcmp(sender.view.state->name, name.c_str()) == 0);
        REQUIRE(std::strcmp(sender.view.state->uuid, identity.uuid) == 0);
    }
};

bool directory_contains(const char *uuid) {
    auto snapshot = registry::read_directory_snapshot();
    REQUIRE(snapshot.ok());
    for (uint32_t i = 0; i < snapshot.value().capacity; ++i) {
        const auto &entry = snapshot.value().entries[i];
        if (entry.valid == nd::entry_valid_flag::valid
            && std::memcmp(entry.uuid, uuid, 37) == 0) return true;
    }
    return false;
}

void check_backing(const owned_sender &sender, bool exists) {
    errno = 0;
    const int fd = ::shm_open(sender.identity.shm_name, O_RDONLY, 0);
    const int error = errno;
    if (exists) {
        CHECK(fd >= 0);
        if (fd >= 0) {
            struct stat reopened{}, retained{};
            const int reopened_result = ::fstat(fd, &reopened);
            const int retained_result = ::fstat(sender.view.handle.fd, &retained);
            CHECK(reopened_result == 0);
            CHECK(retained_result == 0);
            if (reopened_result == 0 && retained_result == 0) {
                CHECK(reopened.st_dev == retained.st_dev);
                CHECK(reopened.st_ino == retained.st_ino);
                CHECK(reopened.st_size == retained.st_size);
            }
        }
    } else {
        CHECK(fd == -1);
        CHECK(error == ENOENT);
    }
    if (fd >= 0) ::close(fd);
}

struct replaced_pid {
    const registry::Registration &identity;
    uint64_t original{};
    bool changed{false};

    ~replaced_pid() {
        if (!changed) return;
        directory_access directory;
        if (!directory.open()) return;
        auto *entry = directory.find(identity.uuid);
        if (entry) entry->pid = original;
    }

    void set(pid_t dead_pid) {
        directory_access directory;
        REQUIRE(directory.open());
        auto *entry = directory.find(identity.uuid);
        REQUIRE(entry != nullptr);
        original = entry->pid;
        REQUIRE(original == ipc::current_pid());
        entry->pid = static_cast<uint64_t>(dead_pid);
        changed = true;
    }
};
} // namespace

TEST_CASE("Stale cleanup: reaped owner is unlinked without revoking retained views",
          "[registry][stale_cleanup]") {
    owned_sender live;
    live.create();
    child_owner dead;
    dead.start();
    std::array<unsigned char, sizeof(nd::SenderSharedState)> before{};
    std::memcpy(before.data(), dead.sender.view.state, before.size());
    REQUIRE(directory_contains(dead.sender.identity.uuid));
    REQUIRE(dead.kill_and_reap());
    REQUIRE_FALSE(ipc::is_pid_alive(static_cast<uint64_t>(dead.pid)));

    SECTION("registration sweeps") {
        owned_sender trigger;
        trigger.create();
    }
    SECTION("explicit cleanup sweeps") {
        registry::cleanup_stale_entries();
    }

    // Baseline invalidates discovery but leaves the POSIX namespace object.
    check_backing(dead.sender, false);
    CHECK_FALSE(directory_contains(dead.sender.identity.uuid));
    CHECK(std::memcmp(dead.sender.view.state, before.data(), before.size()) == 0);
    check_backing(live, true);
    CHECK(directory_contains(live.identity.uuid));
    CHECK(std::strcmp(live.view.state->uuid, live.identity.uuid) == 0);
    CHECK(live.view.state->width == 4);
}

TEST_CASE("Stale cleanup: directory PID alone cannot authorize unlinking a live owner",
          "[registry][stale_cleanup]") {
    owned_sender live;
    live.create();
    child_owner dead;
    dead.start();
    REQUIRE(dead.kill_and_reap());
    REQUIRE_FALSE(ipc::is_pid_alive(static_cast<uint64_t>(dead.pid)));
    std::array<unsigned char, sizeof(nd::SenderSharedState)> before{};
    std::memcpy(before.data(), live.view.state, before.size());
    replaced_pid replacement{live.identity};
    replacement.set(dead.pid);

    SECTION("registration distrusts substituted PID") {
        owned_sender trigger;
        trigger.create();
    }
    SECTION("explicit cleanup distrusts substituted PID") {
        registry::cleanup_stale_entries();
    }

    // Only this test's directory PID changed. The real owner is still alive;
    // all shared-state identity bytes remain genuine and must be preserved.
    check_backing(live, true);
    CHECK(directory_contains(live.identity.uuid));
    CHECK(std::memcmp(live.view.state, before.data(), before.size()) == 0);
}

#endif
