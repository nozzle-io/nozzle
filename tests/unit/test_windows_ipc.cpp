#include <catch2/catch_test_macros.hpp>

#include "ipc.hpp"

#include <cstring>
#include <string>
#include <thread>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

namespace ipc = nozzle::detail::ipc;

namespace {

constexpr std::size_t mapping_size = 4096;
constexpr unsigned loop_count = 64;

struct native_handle {
    HANDLE value{nullptr};
    explicit native_handle(HANDLE handle) : value{handle} {}
    ~native_handle() {
        if (value) {
            CloseHandle(value);
        }
    }
    native_handle(const native_handle &) = delete;
    native_handle &operator=(const native_handle &) = delete;
};

struct child_process {
    PROCESS_INFORMATION info{};
    ~child_process() {
        if (info.hProcess) {
            if (WaitForSingleObject(info.hProcess, 0) != WAIT_OBJECT_0) {
                TerminateProcess(info.hProcess, 99);
                WaitForSingleObject(info.hProcess, 5000);
            }
            CloseHandle(info.hProcess);
        }
        if (info.hThread) {
            CloseHandle(info.hThread);
        }
    }
    child_process() = default;
    child_process(const child_process &) = delete;
    child_process &operator=(const child_process &) = delete;
};

struct mapping_name {
    std::string value;
    mapping_name() {
        static unsigned sequence = 0;
        value = "nozzle_windows_ipc_" + std::to_string(GetCurrentProcessId())
            + "_" + std::to_string(GetTickCount64())
            + "_" + std::to_string(sequence++);
    }
    ~mapping_name() { ipc::shm_unlink(value.c_str()); }
    mapping_name(const mapping_name &) = delete;
    mapping_name &operator=(const mapping_name &) = delete;
};

struct mapping {
    ipc::shm_handle handle{};
    void *view{nullptr};
    explicit mapping(ipc::shm_handle value) : handle{value} {}
    ~mapping() { close(); }
    void close() {
        if (view) {
            ipc::shm_unmap(view, mapping_size);
            view = nullptr;
        }
        ipc::shm_close(handle);
    }
    void map(bool read_only) {
        auto result = ipc::shm_map(handle, mapping_size, read_only);
        REQUIRE(result.ok());
        view = result.value();
    }
    mapping(const mapping &) = delete;
    mapping &operator=(const mapping &) = delete;
};

void create_and_close(const char *name) {
    auto result = ipc::shm_create(name, mapping_size);
    REQUIRE(result.ok());
    mapping owner{result.value()};
    REQUIRE(owner.handle.valid());
}

DWORD handle_count() {
    DWORD count = 0;
    REQUIRE(GetProcessHandleCount(GetCurrentProcess(), &count) != FALSE);
    return count;
}

bool named_mapping_absent(const char *name) {
    native_handle probe{OpenFileMappingA(FILE_MAP_READ, FALSE, name)};
    const DWORD error = GetLastError();
    return probe.value == nullptr && error == ERROR_FILE_NOT_FOUND;
}

} // namespace

TEST_CASE("Windows IPC: recover and release an abandoned directory mutex", "[windows_ipc]") {
    mapping_name name;
    const std::string mutex_name = "Global\\nozzle_lock_" + name.value;
    native_handle keeper{CreateMutexA(nullptr, FALSE, mutex_name.c_str())};
    REQUIRE(keeper.value != nullptr);

    std::vector<wchar_t> executable(32768);
    const DWORD length = GetModuleFileNameW(nullptr, executable.data(),
                                          static_cast<DWORD>(executable.size()));
    REQUIRE(length != 0);
    REQUIRE(length < executable.size());
    const std::wstring current(executable.data(), length);
    const auto separator = current.find_last_of(L"\\/");
    REQUIRE(separator != std::wstring::npos);
    const std::wstring helper = current.substr(0, separator + 1)
        + L"nozzle_windows_ipc_owner.exe";
    std::wstring command = L"\"" + helper + L"\" \""
        + std::wstring(mutex_name.begin(), mutex_name.end()) + L"\"";

    child_process child;
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    const BOOL started = CreateProcessW(helper.c_str(), command.data(), nullptr,
        nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &child.info);
    const DWORD start_error = started ? ERROR_SUCCESS : GetLastError();
    INFO("CreateProcessW error: " << start_error);
    REQUIRE(started != FALSE);
    REQUIRE(WaitForSingleObject(child.info.hProcess, 10000) == WAIT_OBJECT_0);
    DWORD exit_code = STILL_ACTIVE;
    REQUIRE(GetExitCodeProcess(child.info.hProcess, &exit_code) != FALSE);
    REQUIRE(exit_code == 0);

    // The child has exited, so the real IPC wait cannot block on a live owner.
    auto acquired = ipc::dir_lock_acquire(name.value.c_str(), true);
    const bool recovered = acquired.ok();
    bool valid = false;
    bool released = false;
    if (recovered) {
        valid = acquired.value().valid();
        ipc::dir_lock_release(acquired.value());
        released = !acquired.value().valid();
    } else {
        // ed13875 closes its handle on WAIT_ABANDONED, but this thread still
        // owns the mutex. Release via the keeper BEFORE reporting failure.
        ReleaseMutex(keeper.value);
    }

    DWORD next_wait = WAIT_FAILED;
    BOOL next_release = FALSE;
    std::thread next_owner([&] {
        next_wait = WaitForSingleObject(keeper.value, 2000);
        if (next_wait == WAIT_OBJECT_0 || next_wait == WAIT_ABANDONED) {
            next_release = ReleaseMutex(keeper.value);
        }
    });
    next_owner.join();
    // Also clean up if a broken dir_lock_release left parent ownership behind.
    ReleaseMutex(keeper.value);
    CHECK(recovered);
    if (recovered) {
        CHECK(valid);
        CHECK(released);
    }
    CHECK(next_wait == WAIT_OBJECT_0);
    CHECK(next_release != FALSE);
}

TEST_CASE("Windows IPC: repeated normalized creates retain only one pin", "[windows_ipc]") {
    mapping_name name;
    const std::string slash_name = "/" + name.value;
    // Warm IPC and assertion machinery before the measured fixed loop.
    for (unsigned i = 0; i < 4; ++i) {
        create_and_close(name.value.c_str());
    }
    const DWORD before = handle_count();
    for (unsigned i = 0; i < loop_count; ++i) {
        create_and_close(i % 2 == 0 ? slash_name.c_str() : name.value.c_str());
    }
    const DWORD after = handle_count();
    INFO("handles before=" << before << ", after=" << after);
    CHECK(after == before);

    auto opened = ipc::shm_open_ro(slash_name.c_str());
    REQUIRE(opened.ok());
    mapping reader{opened.value()};
    REQUIRE(reader.handle.valid());
    reader.close();
    ipc::shm_unlink(slash_name.c_str());
    CHECK(named_mapping_absent(name.value.c_str()));
    // With all public handles closed, unlink must remove exactly the one pin.
    CHECK(handle_count() + 1 == after);
}

TEST_CASE("Windows IPC: create close unlink churn removes pins and names", "[windows_ipc]") {
    {
        mapping_name warm;
        create_and_close(warm.value.c_str());
        ipc::shm_unlink(warm.value.c_str());
        (void)named_mapping_absent(warm.value.c_str());
    }
    const DWORD before = handle_count();
    unsigned remaining_names = 0;
    for (unsigned i = 0; i < loop_count; ++i) {
        mapping_name name;
        create_and_close(name.value.c_str());
        ipc::shm_unlink(name.value.c_str());
        if (!named_mapping_absent(name.value.c_str())) {
            ++remaining_names;
        }
    }
    const DWORD after = handle_count();
    INFO("handles before=" << before << ", after=" << after);
    CHECK(remaining_names == 0);
    CHECK(after == before);
}

TEST_CASE("Windows IPC: reader survives unlink until its view and handle close", "[windows_ipc]") {
    mapping_name name;
    auto created = ipc::shm_create(name.value.c_str(), mapping_size);
    REQUIRE(created.ok());
    mapping owner{created.value()};
    REQUIRE(owner.handle.valid());
    owner.map(false);
    const char payload[] = "reader remains valid after unlink";
    std::memcpy(owner.view, payload, sizeof(payload));

    auto opened = ipc::shm_open_ro(name.value.c_str());
    REQUIRE(opened.ok());
    mapping reader{opened.value()};
    REQUIRE(reader.handle.valid());
    reader.map(true);
    owner.close();
    ipc::shm_unlink(name.value.c_str());
    CHECK(std::memcmp(reader.view, payload, sizeof(payload)) == 0);
    reader.close();
    CHECK(named_mapping_absent(name.value.c_str()));
}

TEST_CASE("Windows IPC: creating an existing mapping preserves its bytes", "[windows_ipc]") {
    mapping_name name;
    auto created = ipc::shm_create(name.value.c_str(), mapping_size);
    REQUIRE(created.ok());
    mapping owner{created.value()};
    REQUIRE(owner.handle.valid());
    owner.map(false);
    const char payload[] = "existing mapping contents";
    std::memcpy(owner.view, payload, sizeof(payload));

    const std::string slash_name = "/" + name.value;
    auto existing = ipc::shm_create(slash_name.c_str(), mapping_size * 2);
    REQUIRE(existing.ok());
    mapping second{existing.value()};
    REQUIRE(second.handle.valid());
    second.map(false);
    CHECK(std::memcmp(second.view, payload, sizeof(payload)) == 0);
    static_cast<char *>(second.view)[0] = 'E';
    CHECK(static_cast<const char *>(owner.view)[0] == 'E');
}
