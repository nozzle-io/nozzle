#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <initializer_list>
#include <limits>

#include "ipc.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <string>
#include <vector>
#else
#include <cerrno>
#include <signal.h>
#endif

namespace ipc = nozzle::detail::ipc;

TEST_CASE("Process instance: current native token is nonzero and stable", "[process_instance]") {
    auto first = ipc::process_instance_token(ipc::current_pid());
    INFO(first.error().message);
    REQUIRE(first.ok());
    REQUIRE(first.value() != 0);
    auto second = ipc::process_instance_token(ipc::current_pid());
    INFO(second.error().message);
    REQUIRE(second.ok());
    CHECK(second.value() == first.value());
}

TEST_CASE("Process instance: invalid PID is not classified as an exited owner", "[process_instance]") {
    for (const uint64_t pid : {uint64_t{0}, (std::numeric_limits<uint64_t>::max)()}) {
        CAPTURE(pid);
        auto token = ipc::process_instance_token(pid);
        REQUIRE_FALSE(token.ok());
        CHECK(token.error().code == nozzle::ErrorCode::InvalidArgument);
    }
}

TEST_CASE("Process instance: verified native permission denial is not death", "[process_instance]") {
#if defined(_WIN32)
    const uint64_t protected_pid = 4;
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE,
                                 FALSE, static_cast<DWORD>(protected_pid));
    const DWORD error = GetLastError();
    if (process) CloseHandle(process);
    if (process || error != ERROR_ACCESS_DENIED) {
        SKIP("PID 4 did not produce verified native access denial on this host");
    }
#else
    const uint64_t protected_pid = 1;
    errno = 0;
    const int result = ::kill(1, 0);
    const int error = errno;
    if (result != -1 || error != EPERM) {
        SKIP("PID 1 did not produce verified native permission denial on this host");
    }
#endif
    auto token = ipc::process_instance_token(protected_pid);
    if (token.ok()) {
        CHECK(token.value() != 0);
    } else {
        CHECK(token.error().code == nozzle::ErrorCode::BackendError);
    }
}

#if defined(_WIN32)
TEST_CASE("Process instance: exited Windows child is closed while its handle remains",
          "[process_instance]") {
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
            if (info.hThread) CloseHandle(info.hThread);
        }
    } child;
    std::vector<wchar_t> executable(32768);
    const auto length = GetModuleFileNameW(nullptr, executable.data(),
                                          static_cast<DWORD>(executable.size()));
    REQUIRE(length > 0);
    REQUIRE(length < executable.size());
    const std::wstring current(executable.data(), length);
    const auto separator = current.find_last_of(L"\\/");
    REQUIRE(separator != std::wstring::npos);
    const auto helper = current.substr(0, separator + 1) + L"nozzle_windows_ipc_owner.exe";
    // Its existing no-argument path exits with 2 without opening a mutex.
    std::wstring command = L"\"" + helper + L"\"";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    REQUIRE(CreateProcessW(helper.c_str(), command.data(), nullptr, nullptr, FALSE,
                          CREATE_NO_WINDOW, nullptr, nullptr, &startup, &child.info) != FALSE);
    REQUIRE(WaitForSingleObject(child.info.hProcess, 5000) == WAIT_OBJECT_0);
    DWORD exit_code{};
    REQUIRE(GetExitCodeProcess(child.info.hProcess, &exit_code) != FALSE);
    REQUIRE(exit_code == 2);
    auto token = ipc::process_instance_token(child.info.dwProcessId);
    REQUIRE_FALSE(token.ok());
    CHECK(token.error().code == nozzle::ErrorCode::SenderClosed);
}
#endif
