#include <catch2/catch_test_macros.hpp>

#include "ipc.hpp"
#include "metadata.hpp"
#include "registry.hpp"
#include "shared_state.hpp"

#include <nozzle/types.hpp>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace {
namespace ipc = nozzle::detail::ipc;
namespace registry = nozzle::detail::registry;
namespace nd = nozzle::detail;
constexpr std::size_t directory_size = sizeof(nd::DirectoryHeader)
    + sizeof(nd::DirectoryEntry) * nd::kMaxSenders;

std::size_t runtime_page_size() {
#if defined(_WIN32)
    SYSTEM_INFO system_info{};
    GetSystemInfo(&system_info);
    return system_info.dwPageSize;
#else
    const long size = sysconf(_SC_PAGESIZE);
    return size > 0 ? static_cast<std::size_t>(size) : 0;
#endif
}

struct mapped_region {
    ipc::shm_handle handle{};
    void *data{nullptr};
    std::size_t size{0};
    ~mapped_region() { close(); }
    void close() {
        if (data) ipc::shm_unmap(data, size);
        data = nullptr;
        ipc::shm_close(handle);
    }
    bool map(std::size_t length) {
        size = length;
        auto result = ipc::shm_map(handle, size, false);
        if (!result.ok()) return false;
        data = result.value();
        return true;
    }
    bool create(const char *name, std::size_t length) {
        auto result = ipc::shm_create(name, length);
        if (!result.ok()) return false;
        handle = result.value();
        return map(length);
    }
};

// These CI cases temporarily own the global directory, just like the existing
// registry suites. Save its bytes; never ask corrupt production code to clean it.
struct layout_fixture {
    mapped_region directory;
    mapped_region state;
    std::vector<unsigned char> saved_directory;
    registry::Registration registration{};
    std::string sender_name = "layout_" + std::to_string(ipc::current_pid())
        + "_" + std::to_string(ipc::monotonic_ns());
    bool active{false};
    bool replaced{false};

    ~layout_fixture() { restore(); }

    bool create() {
        auto existing = ipc::shm_open_rw(nd::kDirectoryShmName);
        if (existing.ok()) {
            directory.handle = existing.value();
            if (!directory.map(directory_size)) return false;
            saved_directory.resize(directory_size);
            std::memcpy(saved_directory.data(), directory.data, directory_size);
        } else {
            // Mark ownership before allocation so even failed mapping is cleaned.
            active = true;
            if (!directory.create(nd::kDirectoryShmName, directory_size)) return false;
        }
        active = true;
        std::memset(directory.data, 0, directory_size);
        header().magic = nd::kDirectoryMagic;
        header().version = 6;
        header().capacity = nd::kMaxSenders;
        auto registered = registry::register_sender(sender_name.c_str(), "layout_tests",
            0, 8, 8, static_cast<uint32_t>(nozzle::texture_format::rgba8_unorm), 2);
        if (!registered.ok()) return false;
        registration = registered.value();
        auto opened = ipc::shm_open_rw(registration.shm_name);
        if (!opened.ok()) return false;
        state.handle = opened.value();
        return state.map(sizeof(nd::SenderSharedState));
    }

    nd::DirectoryHeader &header() {
        return *static_cast<nd::DirectoryHeader *>(directory.data);
    }
    nd::DirectoryEntry &entry(std::size_t index = 0) {
        return reinterpret_cast<nd::DirectoryEntry *>(
            static_cast<unsigned char *>(directory.data) + sizeof(nd::DirectoryHeader))[index];
    }
    nd::SenderSharedState &sender_state() {
        return *static_cast<nd::SenderSharedState *>(state.data);
    }

    bool shorten_directory(bool wrong_version = false) {
        directory.close();
        ipc::shm_unlink(nd::kDirectoryShmName);
        replaced = true;
        if (wrong_version) {
            const uint32_t prefix[]{nd::kDirectoryMagic, 7};
            if (!directory.create(nd::kDirectoryShmName, sizeof(prefix))) return false;
            std::memcpy(directory.data, prefix, sizeof(prefix));
        } else {
            // Remove at least one runtime page, so page rounding cannot make
            // the full directory fit as it can for a subpage sender state.
            const auto page_size = runtime_page_size();
            if (page_size == 0 || directory_size <= page_size + sizeof(nd::DirectoryHeader)) return false;
            const auto short_size = directory_size - page_size;
            if (!directory.create(nd::kDirectoryShmName, short_size)) return false;
            std::memset(directory.data, 0, short_size);
            header() = nd::DirectoryHeader{};
            header().magic = nd::kDirectoryMagic;
            header().version = 6;
            header().capacity = nd::kMaxSenders;
        }
        return true;
    }

    bool shorten_state(bool wrong_version = false) {
        std::vector<unsigned char> bytes(sizeof(nd::SenderSharedState) - 1);
        std::memcpy(bytes.data(), state.data, bytes.size());
        state.close();
        ipc::shm_unlink(registration.shm_name);
        if (wrong_version) {
            const uint32_t prefix[]{nd::kSenderMagic, static_cast<uint32_t>(nd::kSharedMemVersion + 1)};
            if (!state.create(registration.shm_name, sizeof(prefix))) return false;
            std::memcpy(state.data, prefix, sizeof(prefix));
        } else {
            // All checked fields remain valid: rejecting a zero-filled ring
            // must not accidentally substitute for checking the mapping size.
            // Linux exposes this exact byte extent. macOS/Windows expose
            // page-rounded backing, so a full-state view is a valid control
            // there, not evidence that the original request was 1664 bytes.
            if (!state.create(registration.shm_name, bytes.size())) return false;
            std::memcpy(state.data, bytes.data(), bytes.size());
        }
        return true;
    }

    bool use_one_page_state() {
        const auto page_size = runtime_page_size();
        if (page_size < sizeof(nd::SenderSharedState)) return false;
        const auto original = sender_state();
        state.close();
        ipc::shm_unlink(registration.shm_name);
        if (!state.create(registration.shm_name, page_size)) return false;
        std::memcpy(state.data, &original, sizeof(original));
        return true;
    }

    void remove_state() {
        state.close();
        ipc::shm_unlink(registration.shm_name);
    }

#if !defined(_WIN32)
    bool zero_length_state() {
        remove_state();
        // Model O_EXCL-before-ftruncate, not an object allocated by ftruncate(0).
        state.handle.fd = ::shm_open(registration.shm_name, O_RDWR | O_CREAT | O_EXCL, 0600);
        state.size = 0;
        return state.handle.valid();
    }
    bool zero_length_directory() {
        directory.close();
        ipc::shm_unlink(nd::kDirectoryShmName);
        replaced = true;
        directory.handle.fd = ::shm_open(nd::kDirectoryShmName, O_RDWR | O_CREAT | O_EXCL, 0600);
        directory.size = 0;
        return directory.handle.valid();
    }
#endif

    void cleanup_probe_registration() {
        // The child may have died after registering. Only remove the named
        // probe's mapping; never route cleanup through a corrupt directory.
        mapped_region current;
        auto opened = ipc::shm_open_rw(nd::kDirectoryShmName);
        if (!opened.ok()) return;
        current.handle = opened.value();
#if defined(_WIN32)
        current.size = directory_size;
        current.data = MapViewOfFile(current.handle.handle, FILE_MAP_READ, 0, 0, directory_size);
        if (!current.data) return;
#else
        // Parent cleanup must not trust the IPC extent check being tested.
        struct stat backing{};
        if (fstat(current.handle.fd, &backing) != 0 || backing.st_size < 0
            || static_cast<std::uintmax_t>(backing.st_size) < directory_size) return;
        if (!current.map(directory_size)) return;
#endif
        const auto *header = static_cast<const nd::DirectoryHeader *>(current.data);
        if (header->magic != nd::kDirectoryMagic || header->capacity > nd::kMaxSenders) return;
        const auto *entries = reinterpret_cast<const nd::DirectoryEntry *>(
            static_cast<const unsigned char *>(current.data) + sizeof(nd::DirectoryHeader));
        for (uint32_t i = 0; i < header->capacity; ++i) {
            const auto &entry = entries[i];
            if (entry.valid == nd::entry_valid_flag::valid
                && std::strncmp(entry.sender_name, "layout_probe_extra", sizeof(entry.sender_name)) == 0
                && std::memchr(entry.shm_name, '\0', sizeof(entry.shm_name))) {
                ipc::shm_unlink(entry.shm_name);
            }
        }
    }

    bool restore() {
        if (!active) return true;
        cleanup_probe_registration();
        state.close();
        if (registration.shm_name[0]) ipc::shm_unlink(registration.shm_name);
        if (replaced) {
            directory.close();
            ipc::shm_unlink(nd::kDirectoryShmName);
            if (!saved_directory.empty()
                && !directory.create(nd::kDirectoryShmName, directory_size)) return false;
            replaced = false;
        }
        if (!saved_directory.empty()) {
            if (!directory.data) return false;
            std::memcpy(directory.data, saved_directory.data(), saved_directory.size());
        }
        directory.close();
        if (saved_directory.empty()) ipc::shm_unlink(nd::kDirectoryShmName);
        active = false;
        return true;
    }
};

void require_setup(layout_fixture &fixture, bool ready) {
    // Strict test translation units cannot rely on exception unwinding.
    // Restore before any fatal assertion, including setup failures.
    if (!ready) {
        const bool restored = fixture.restore();
        REQUIRE(restored);
    }
    REQUIRE(ready);
}

struct probe_result {
    bool started{false};
    bool completed{false};
    bool reaped{false};
    unsigned long status{0};
};

probe_result run_probe(const char *operation, const layout_fixture &fixture) {
    probe_result result;
#if defined(_WIN32)
    const std::string executable = NOZZLE_SHARED_LAYOUT_PROBE_PATH;
    std::string command = "\"" + executable + "\" " + operation + " "
        + fixture.sender_name + " " + fixture.registration.shm_name;
    STARTUPINFOA startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION child{};
    result.started = CreateProcessA(executable.c_str(), command.data(), nullptr,
        nullptr, FALSE, 0, nullptr, nullptr, &startup, &child) != FALSE;
    if (!result.started) {
        result.status = GetLastError();
        return result;
    }
    result.completed = WaitForSingleObject(child.hProcess, 10000) == WAIT_OBJECT_0;
    if (!result.completed) TerminateProcess(child.hProcess, 99);
    result.reaped = WaitForSingleObject(child.hProcess, 5000) == WAIT_OBJECT_0;
    DWORD status = STILL_ACTIVE;
    if (!GetExitCodeProcess(child.hProcess, &status)) status = GetLastError();
    result.status = status;
    CloseHandle(child.hThread);
    CloseHandle(child.hProcess);
#else
    const pid_t child = fork();
    if (child == 0) {
        execl(NOZZLE_SHARED_LAYOUT_PROBE_PATH, NOZZLE_SHARED_LAYOUT_PROBE_PATH,
            operation, fixture.sender_name.c_str(), fixture.registration.shm_name,
            static_cast<char *>(nullptr));
        _exit(127);
    }
    result.started = child > 0;
    if (!result.started) {
        result.status = static_cast<unsigned long>(errno);
        return result;
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    int status = 0;
    while (std::chrono::steady_clock::now() < deadline) {
        const pid_t waited = waitpid(child, &status, WNOHANG);
        if (waited == child) {
            result.completed = result.reaped = true;
            break;
        }
        if (waited < 0 && errno != EINTR) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (!result.reaped) {
        kill(child, SIGKILL);
        pid_t waited;
        do { waited = waitpid(child, &status, 0); } while (waited < 0 && errno == EINTR);
        result.reaped = waited == child;
    }
    result.status = WIFEXITED(status) ? static_cast<unsigned long>(WEXITSTATUS(status))
        : WIFSIGNALED(status) ? 128UL + static_cast<unsigned long>(WTERMSIG(status)) : 255UL;
#endif
    return result;
}

void check_probe(const probe_result &result) {
    INFO("probe status=" << result.status << " (10=behavior, 20=setup, 127=exec; signals/exceptions remain failures)");
    CHECK(result.started);
    CHECK(result.completed);
    CHECK(result.reaped);
    CHECK(result.status == 0);
}

void corrupt_directory_field(nd::DirectoryEntry &entry, const char *field) {
    if (std::strcmp(field, "name") == 0) std::memset(entry.sender_name, 'n', sizeof(entry.sender_name));
    if (std::strcmp(field, "application") == 0) std::memset(entry.application_name, 'a', sizeof(entry.application_name));
    if (std::strcmp(field, "uuid") == 0) std::memset(entry.uuid, 'u', sizeof(entry.uuid));
    if (std::strcmp(field, "shm_name") == 0) std::memset(entry.shm_name, 's', sizeof(entry.shm_name));
}

#if !defined(_WIN32)
struct blocked_lock_path {
    const char *path = "/tmp/.nozzle_lock_nozzle_dir";
    std::string backup_directory;
    std::string backup_path;
    bool moved{false};
    bool blocked{false};
    ~blocked_lock_path() { restore(); }

    bool block() {
        struct stat original{};
        if (lstat(path, &original) != 0 || !S_ISREG(original.st_mode)) return false;
        char temporary[] = "/tmp/nozzle_layout_lock_XXXXXX";
        const char *created = mkdtemp(temporary);
        if (!created) return false;
        backup_directory = created;
        backup_path = backup_directory + "/original";
        if (std::rename(path, backup_path.c_str()) != 0) return false;
        moved = true;
        if (mkdir(path, 0700) != 0) return false;
        blocked = true;
        // Unlike chmod, a directory rejects O_RDWR even for a root CI runner.
        const int fd = open(path, O_RDWR | O_CREAT, 0666);
        const int error = errno;
        if (fd >= 0) close(fd);
        return fd < 0 && error == EISDIR;
    }

    bool restore() {
        if (blocked) {
            if (rmdir(path) != 0) return false;
            blocked = false;
        }
        if (moved) {
            struct stat occupant{};
            // Preserve an unexpected occupant instead of overwriting it.
            if (lstat(path, &occupant) == 0 || errno != ENOENT) return false;
            if (std::rename(backup_path.c_str(), path) != 0) return false;
            moved = false;
        }
        if (!backup_directory.empty()) {
            if (rmdir(backup_directory.c_str()) != 0) return false;
            backup_directory.clear();
        }
        return true;
    }
};
#endif
} // namespace

TEST_CASE("Shared layout: CPU-only receiver accepts a registered sender", "[shared_layout][integration]") {
    layout_fixture fixture;
    require_setup(fixture, fixture.create());
    SECTION("maximum directory capacity") {}
    SECTION("one-entry directory capacity") { fixture.header().capacity = 1; }
    const auto result = run_probe("control", fixture);
    REQUIRE(fixture.restore());
    check_probe(result);
}

TEST_CASE("Shared layout: receiver rejects a mapped state with another identity", "[shared_layout][integration]") {
    layout_fixture fixture;
    require_setup(fixture, fixture.create());
    SECTION("mapped state uuid differs from copied directory entry") {
        auto &last = fixture.sender_state().uuid[35];
        last = last == '0' ? '1' : '0';
    }
    SECTION("mapped state name differs from requested sender") {
        auto &name = fixture.sender_state().name;
        std::memset(name, 0, sizeof(name));
        std::memcpy(name, "another_sender", sizeof("another_sender"));
    }
    // The entry still names a real mapping with valid header, ring and strings.
    const auto result = run_probe("connect_not_found", fixture);
    REQUIRE(fixture.restore());
    check_probe(result);
}

TEST_CASE("Shared layout: readers finish while another process owns the writer lock", "[shared_layout][integration]") {
    for (const char *operation : {"control", "enumerate_control"}) {
        DYNAMIC_SECTION(operation) {
            layout_fixture fixture;
            require_setup(fixture, fixture.create());
            auto held = ipc::dir_lock_acquire(nd::kDirectoryShmName, true);
            if (!held.ok()) require_setup(fixture, false);
            // The parent owns the actual OS lock; the existing child deadline
            // kills and reaps a reader blocked by the baseline implementation.
            const auto result = run_probe(operation, fixture);
#if defined(_WIN32)
            const bool unlocked = ReleaseMutex(held.value().handle) != FALSE;
            const bool closed = CloseHandle(held.value().handle) != FALSE;
#else
            struct flock unlock{};
            unlock.l_type = F_UNLCK;
            unlock.l_whence = SEEK_SET;
            int unlock_result;
            do { unlock_result = fcntl(held.value().fd, F_SETLK, &unlock); }
            while (unlock_result < 0 && errno == EINTR);
            const bool unlocked = unlock_result == 0;
            const bool closed = close(held.value().fd) == 0;
#endif
            held.value() = {};
            const bool restored = fixture.restore();
            CHECK(unlocked);
            CHECK(closed);
            REQUIRE(restored);
            check_probe(result);
        }
    }
}

#if !defined(_WIN32)
TEST_CASE("Shared layout: lookup does not need a writable POSIX lock path", "[shared_layout][integration]") {
    layout_fixture fixture;
    require_setup(fixture, fixture.create());
    blocked_lock_path lock_path;
    const bool blocked = lock_path.block();
    probe_result result;
    if (blocked) result = run_probe("control", fixture);
    // Restore the original inode before registry cleanup or any fatal assertion.
    const bool path_restored = lock_path.restore();
    const bool registry_restored = fixture.restore();
    INFO("original lock backup: " << lock_path.backup_path);
    REQUIRE(path_restored);
    REQUIRE(registry_restored);
    REQUIRE(blocked);
    check_probe(result);
}
#endif

TEST_CASE("Shared layout: directory headers are rejected without resetting existing bytes", "[shared_layout][integration]") {
    for (const char *corruption : {"version", "magic", "zero_capacity", "large_capacity"}) {
        for (const char *operation : {"enumerate", "register", "connect"}) {
            DYNAMIC_SECTION(corruption << " / " << operation) {
                layout_fixture fixture;
                require_setup(fixture, fixture.create());
                const bool version = std::strcmp(corruption, "version") == 0;
                if (version) fixture.header().version = 7;
                if (std::strcmp(corruption, "magic") == 0) fixture.header().magic = nd::kDirectoryMagic ^ 1u;
                if (std::strcmp(corruption, "zero_capacity") == 0) fixture.header().capacity = 0;
                if (std::strcmp(corruption, "large_capacity") == 0) fixture.header().capacity = nd::kMaxSenders + 1;
                std::vector<unsigned char> before(directory_size);
                std::memcpy(before.data(), fixture.directory.data, before.size());
                const std::string probe = std::string(operation)
                    + (version && std::strcmp(operation, "enumerate") != 0 ? "_version" : "");
                const auto result = run_probe(probe.c_str(), fixture);
                const bool intact = std::memcmp(before.data(), fixture.directory.data, before.size()) == 0;
                REQUIRE(fixture.restore());
                check_probe(result);
                CHECK(intact);
            }
        }
    }
}

TEST_CASE("Shared layout: receiver waits for absent or unpublished sender state", "[shared_layout][integration]") {
    for (const char *phase : {"absent", "zeroed"}) {
        DYNAMIC_SECTION(phase) {
            layout_fixture fixture;
            require_setup(fixture, fixture.create());
            // Keep the real directory entry valid, as older senders do during startup.
            if (std::strcmp(phase, "absent") == 0) fixture.remove_state();
            else std::memset(fixture.state.data, 0, sizeof(nd::SenderSharedState));
            const auto result = run_probe("connect_not_found", fixture);
            REQUIRE(fixture.restore());
            check_probe(result);
        }
    }
}

TEST_CASE("Shared layout: unpublished directory remains retryable for receivers", "[shared_layout][integration]") {
    for (const char *phase : {"zero_prefix", "partial_header"}) {
        DYNAMIC_SECTION(phase) {
            layout_fixture fixture;
            require_setup(fixture, fixture.create());
            fixture.header().magic = 0;
            if (std::strcmp(phase, "zero_prefix") == 0) fixture.header().version = 0;
            const auto result = run_probe("connect_not_found", fixture);
            REQUIRE(fixture.restore());
            check_probe(result);
        }
    }
}

#if !defined(_WIN32)
TEST_CASE("Shared layout: POSIX zero-length backing rejects maps but lookup remains retryable", "[shared_layout][integration]") {
    for (const char *object : {"directory", "sender"}) {
        for (const char *operation : {"map", "connect_not_found"}) {
            DYNAMIC_SECTION(object << " / " << operation) {
                layout_fixture fixture;
                require_setup(fixture, fixture.create());
                const bool directory = std::strcmp(object, "directory") == 0;
                require_setup(fixture, directory ? fixture.zero_length_directory() : fixture.zero_length_state());
                const char *probe = std::strcmp(operation, "map") == 0
                    ? (directory ? "map_directory" : "map_state") : operation;
                const auto result = run_probe(probe, fixture);
                REQUIRE(fixture.restore());
                check_probe(result);
            }
        }
    }
}
#endif

TEST_CASE("Shared layout: registry writer recovers abandoned directory initialization", "[shared_layout][integration]") {
    for (const char *phase : {"zeroed", "partial_header"
#if !defined(_WIN32)
        , "zero_length"
#endif
    }) {
        DYNAMIC_SECTION(phase) {
            layout_fixture fixture;
            require_setup(fixture, fixture.create());
            // Recovery may replace abandoned backing. Restore the named object,
            // not just a view of an inode the writer may have unlinked.
            fixture.replaced = true;
#if !defined(_WIN32)
            if (std::strcmp(phase, "zero_length") == 0) {
                require_setup(fixture, fixture.zero_length_directory());
            } else
#endif
            {
                std::memset(fixture.directory.data, 0, directory_size);
                if (std::strcmp(phase, "partial_header") == 0) {
                    fixture.header().version = 6;
                    fixture.header().capacity = 64;
                }
            }
            // Call the real writer, which acquires the directory's exclusive lock.
            const auto result = run_probe("register_control", fixture);
            REQUIRE(fixture.restore());
            check_probe(result);
        }
    }
}

TEST_CASE("Shared layout: zero magic with foreign body is not writer-recoverable", "[shared_layout][integration]") {
    for (const char *location : {"entry", "tail"}) {
        DYNAMIC_SECTION(location) {
            layout_fixture fixture;
            require_setup(fixture, fixture.create());
            std::memset(fixture.directory.data, 0, directory_size);
            fixture.header().version = 6;
            fixture.header().capacity = 64;
            if (std::strcmp(location, "entry") == 0) fixture.entry().sender_name[0] = 'x';
            else static_cast<unsigned char *>(fixture.directory.data)[directory_size - 1] = 0x5a;
            std::vector<unsigned char> before(directory_size);
            std::memcpy(before.data(), fixture.directory.data, directory_size);
            const auto reader = run_probe("connect_not_found", fixture);
            const auto writer = run_probe("register", fixture);
            const bool intact = std::memcmp(before.data(), fixture.directory.data, directory_size) == 0;
            REQUIRE(fixture.restore());
            check_probe(reader);
            check_probe(writer);
            CHECK(intact);
        }
    }
}

TEST_CASE("Shared layout: directory versions 2 5 and 6 keep existing live entries", "[shared_layout][integration]") {
    for (uint32_t version : {2u, 5u, 6u}) {
        for (const char *operation : {"enumerate_control", "control", "register_control"}) {
            DYNAMIC_SECTION(version << " / " << operation) {
                layout_fixture fixture;
                require_setup(fixture, fixture.create());
                fixture.header().version = version;
                unsigned char original_entry[sizeof(nd::DirectoryEntry)];
                std::memcpy(original_entry, &fixture.entry(), sizeof(original_entry));
                const auto result = run_probe(operation, fixture);
                const bool intact = fixture.header().version == version
                    && fixture.header().capacity == 64
                    && std::memcmp(original_entry, &fixture.entry(), sizeof(original_entry)) == 0;
                REQUIRE(fixture.restore());
                check_probe(result);
                CHECK(intact);
            }
        }
    }
}

TEST_CASE("Shared layout: unterminated directory fields are rejected", "[shared_layout][integration]") {
    for (const char *field : {"name", "application", "uuid", "shm_name"}) {
        for (const char *operation : {"enumerate", "register", "connect"}) {
            DYNAMIC_SECTION(field << " / " << operation) {
                layout_fixture fixture;
                require_setup(fixture, fixture.create());
                corrupt_directory_field(fixture.entry(), field);
                const auto result = run_probe(std::strcmp(operation, "connect") == 0 ? "connect_not_found" : operation, fixture);
                REQUIRE(fixture.restore());
                check_probe(result);
            }
        }
    }
}

TEST_CASE("Shared layout: malformed unrelated entry does not hide a healthy sender", "[shared_layout][integration]") {
    for (const char *field : {"name", "application", "uuid", "shm_name"}) {
        for (const char *operation : {"enumerate_control", "control"}) {
            DYNAMIC_SECTION(field << " / " << operation) {
                layout_fixture fixture;
                require_setup(fixture, fixture.create());
                fixture.entry(1) = fixture.entry();
                std::memset(fixture.entry().sender_name, 0, sizeof(fixture.entry().sender_name));
                std::memcpy(fixture.entry().sender_name, "unrelated", sizeof("unrelated"));
                corrupt_directory_field(fixture.entry(), field);
                const auto result = run_probe(operation, fixture);
                REQUIRE(fixture.restore());
                check_probe(result);
            }
        }
    }
}

TEST_CASE("Shared layout: receiver validates sender fields before connecting", "[shared_layout][integration]") {
    for (const char *field : {"version_2", "version_5", "version_6", "version_future", "lifecycle", "ring_zero", "ring_large", "name", "application", "uuid"}) {
        DYNAMIC_SECTION(field) {
            layout_fixture fixture;
            require_setup(fixture, fixture.create());
            auto &state = fixture.sender_state();
            const bool version = std::strncmp(field, "version_", 8) == 0;
            if (std::strcmp(field, "version_2") == 0) state.version = 2;
            if (std::strcmp(field, "version_5") == 0) state.version = 5;
            if (std::strcmp(field, "version_6") == 0) state.version = 6;
            if (std::strcmp(field, "version_future") == 0) state.version = nd::kSharedMemVersion + 1;
            if (std::strcmp(field, "lifecycle") == 0) state.lifecycle_state = nd::sender_state_closed + 1;
            if (std::strcmp(field, "ring_zero") == 0) state.ring_size = 0;
            if (std::strcmp(field, "ring_large") == 0) state.ring_size = nd::kMaxRingSlots + 1;
            if (std::strcmp(field, "name") == 0) std::memset(state.name, 'n', sizeof(state.name));
            if (std::strcmp(field, "application") == 0) std::memset(state.application_name, 'a', sizeof(state.application_name));
            if (std::strcmp(field, "uuid") == 0) std::memset(state.uuid, 'u', sizeof(state.uuid));
            const auto result = run_probe(version ? "connect_version" : "connect", fixture);
            REQUIRE(fixture.restore());
            check_probe(result);
        }
    }
}

TEST_CASE("Shared layout: undersized directory mappings fail in a bounded child", "[shared_layout][integration]") {
    for (const char *operation : {"enumerate", "register_short", "connect_short"}) {
        DYNAMIC_SECTION(operation) {
            layout_fixture fixture;
            require_setup(fixture, fixture.create());
            require_setup(fixture, fixture.shorten_directory());
            const auto result = run_probe(operation, fixture);
            REQUIRE(fixture.restore());
            check_probe(result);
        }
    }
}

#if defined(__linux__)
TEST_CASE("Shared layout: Linux rejects exact-byte undersized sender backing", "[shared_layout][integration]") {
    for (const char *operation : {"map_state", "open_state", "connect_state_short"}) {
#elif defined(__APPLE__) || defined(_WIN32)
TEST_CASE("Shared layout: macOS and Windows accept valid page-rounded sender backing", "[shared_layout][integration]") {
    for (const char *operation : {"map_state_rounded", "open_state_rounded", "connect_state_rounded"}) {
#endif
#if defined(__linux__) || defined(__APPLE__) || defined(_WIN32)
        DYNAMIC_SECTION(operation) {
            layout_fixture fixture;
            require_setup(fixture, fixture.create());
            require_setup(fixture, fixture.shorten_state());
            const auto result = run_probe(operation, fixture);
            REQUIRE(fixture.restore());
            check_probe(result);
        }
    }
}
#endif

TEST_CASE("Shared layout: mapping beyond one page of backing is rejected", "[shared_layout][integration]") {
    layout_fixture fixture;
    require_setup(fixture, fixture.create());
    require_setup(fixture, fixture.use_one_page_state());
    const auto result = run_probe("map_page_boundary", fixture);
    REQUIRE(fixture.restore());
    check_probe(result);
}

TEST_CASE("Shared layout: small old layouts retain explicit version diagnostics", "[shared_layout][integration]") {
    for (const char *operation : {"enumerate", "register_version", "connect_version", "open_state_version", "connect_state_version"}) {
        DYNAMIC_SECTION(operation) {
            layout_fixture fixture;
            require_setup(fixture, fixture.create());
            if (std::strcmp(operation, "open_state_version") == 0
                || std::strcmp(operation, "connect_state_version") == 0) require_setup(fixture, fixture.shorten_state(true));
            else require_setup(fixture, fixture.shorten_directory(true));
            const auto result = run_probe(operation, fixture);
            REQUIRE(fixture.restore());
            check_probe(result);
        }
    }
}

TEST_CASE("Shared layout: acquisition revalidates ring and committed slot", "[shared_layout][integration]") {
    for (const char *operation : {"ring_zero", "ring_large", "slot_invalid"}) {
        DYNAMIC_SECTION(operation) {
            layout_fixture fixture;
            require_setup(fixture, fixture.create());
            const auto result = run_probe(operation, fixture);
            REQUIRE(fixture.restore());
            check_probe(result);
        }
    }
}

TEST_CASE("Shared layout: metadata terminators must lie inside the supplied extent", "[shared_layout][integration]") {
    const char buffer[] = "key=value";
    CHECK(nd::parse_metadata(buffer, sizeof(buffer) - 1).empty());
    const char prefix[] = "first=valid\0last=truncated";
    const auto parsed = nd::parse_metadata(prefix, sizeof(prefix) - 1);
    REQUIRE(parsed.size() == 1);
    CHECK(parsed[0].key == "first");
    CHECK(parsed[0].value == "valid");
}
