#include <nozzle/nozzle.hpp>
#include <catch2/catch_test_macros.hpp>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include "d3d11_fanout_protocol.hpp"
#include <iostream>

namespace {

using fanout_test::clock;
using fanout_test::reply;
constexpr int timeout_code = static_cast<int>(nozzle::ErrorCode::Timeout);

struct temporary_directory {
    std::filesystem::path path;
    temporary_directory() {
        char root[MAX_PATH]{};
        const DWORD length = GetTempPathA(MAX_PATH, root);
        if (length == 0 || length >= MAX_PATH) return;
        path = std::filesystem::path(root) / ("nozzle_fanout_" +
            std::to_string(GetCurrentProcessId()) + "_" +
            std::to_string(clock::now().time_since_epoch().count()));
        std::error_code error;
        if (!std::filesystem::create_directory(path, error)) path.clear();
    }
    ~temporary_directory() {
        if (!path.empty()) {
            std::error_code error;
            std::filesystem::remove_all(path, error);
        }
    }
};

struct actor {
    HANDLE process{nullptr};
    DWORD pid{0};
    std::filesystem::path directory;
    uint32_t sequence{0};

    actor() = default;
    actor(const actor &) = delete;
    actor &operator=(const actor &) = delete;
    ~actor() {
        if (process) {
            if (WaitForSingleObject(process, 0) == WAIT_TIMEOUT) {
                fanout_test::publish_file(directory / (std::to_string(++sequence) + ".command"), "quit");
                if (WaitForSingleObject(process, 1000) == WAIT_TIMEOUT) {
                    TerminateProcess(process, 99);
                    WaitForSingleObject(process, 3000);
                }
            }
            CloseHandle(process);
        }
    }

    bool start(const std::filesystem::path &root, const char *label,
               const char *role, const std::string &name, uint32_t ring) {
        directory = root / label;
        std::error_code error;
        if (!std::filesystem::create_directory(directory, error)) return false;
        char current[MAX_PATH]{};
        const DWORD length = GetModuleFileNameA(nullptr, current, MAX_PATH);
        if (length == 0 || length >= MAX_PATH) return false;
        const auto executable = std::filesystem::path(current).parent_path() /
            "nozzle_d3d11_cross_process_sender.exe";
        // Paths have no trailing separator and generated names contain no quotes.
        std::string command = "\"" + executable.string() + "\" actor " + role +
            " " + name + " " + std::to_string(ring) + " \"" + directory.string() + "\"";
        STARTUPINFOA startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION info{};
        if (!CreateProcessA(executable.string().c_str(), command.data(), nullptr,
                nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &info)) return false;
        process = info.hProcess;
        pid = info.dwProcessId;
        CloseHandle(info.hThread);
        return true;
    }

    reply wait(const std::filesystem::path &file) {
        std::string text;
        const auto deadline = clock::now() + std::chrono::seconds(8);
        bool received = false;
        while (!(received = fanout_test::read_file(file, text))) {
            if (clock::now() >= deadline || WaitForSingleObject(process, 0) != WAIT_TIMEOUT) break;
            Sleep(2);
        }
        DWORD exit_code = STILL_ACTIVE;
        GetExitCodeProcess(process, &exit_code);
        INFO("actor=" << pid << " file=" << file.string() << " exit=" << exit_code);
        REQUIRE(received);
        reply result;
        REQUIRE(result.decode(text));
        return result;
    }

    void ready() { REQUIRE(wait(directory / "ready").code == 0); }

    reply send(const std::string &command) {
        const auto prefix = std::to_string(++sequence);
        REQUIRE(fanout_test::publish_file(directory / (prefix + ".command"), command));
        auto result = wait(directory / (prefix + ".reply"));
        std::cout << "D3D11_FANOUT pid=" << pid << " command=" << command
                  << " reply=" << result.encode() << std::endl;
        return result;
    }

    void kill() {
        REQUIRE(TerminateProcess(process, 97) != FALSE);
        REQUIRE(WaitForSingleObject(process, 3000) == WAIT_OBJECT_0);
    }
};

struct peers {
    temporary_directory directory;
    actor sender, a, b;
    explicit peers(uint32_t ring) {
        REQUIRE_FALSE(directory.path.empty());
        const auto name = "fanout_" + std::to_string(GetCurrentProcessId()) + "_" +
            std::to_string(clock::now().time_since_epoch().count());
        REQUIRE(sender.start(directory.path, "sender", "sender", name, ring));
        sender.ready();
        REQUIRE(a.start(directory.path, "a", "receiver", name, ring));
        a.ready();
        REQUIRE(b.start(directory.path, "b", "receiver", name, ring));
        b.ready();
        REQUIRE(sender.pid != a.pid);
        REQUIRE(sender.pid != b.pid);
        REQUIRE(a.pid != b.pid);
    }

    reply publish(uint32_t seed, uint64_t forbidden = 0) {
        return sender.send("publish " + std::to_string(seed) + " " + std::to_string(forbidden));
    }
};

void require_publication(const reply &result) {
    INFO("publication=" << result.encode());
    REQUIRE(result.acquire_code == 0);
    REQUIRE(result.commit_code == 0);
    REQUIRE(result.code == 0);
    REQUIRE(result.wrote);
}

void check_frame(const reply &result) {
    INFO("received=" << result.encode());
    CHECK(result.code == 0);
    if (result.code == 0) {
        CHECK(result.frame > 0);
        CHECK(result.pixels_ok);
    }
}

} // namespace

TEST_CASE("D3D11 cross-process fanout delivers identical pixels in either receiver order",
          "[.][integration][cross_process][d3d11_fanout]") {
    peers p(3);
    require_publication(p.publish(1));
    const auto a = p.a.send("acquire 100 1 0");
    const auto b = p.b.send("acquire 100 1 0");
    check_frame(a);
    check_frame(b);
    if (a.code == 0 && b.code == 0) CHECK(a.frame == b.frame);

    require_publication(p.publish(2));
    const auto b_next = p.b.send("acquire 100 2 0");
    const auto a_next = p.a.send("acquire 100 2 0");
    check_frame(b_next);
    check_frame(a_next);
    if (b_next.code == 0 && a_next.code == 0) CHECK(b_next.frame == a_next.frame);
    if (a_next.code == 0 && a.code == 0) CHECK(a_next.frame > a.frame);
}

TEST_CASE("D3D11 cross-process zero timeout is prompt for contention and no new frame",
          "[.][integration][cross_process][d3d11_fanout]") {
    peers p(3);
    require_publication(p.publish(3));
    const auto held = p.a.send("acquire 100 3 1");
    REQUIRE(held.code == 0);
    REQUIRE(held.pixels_ok);
    const auto contended = p.b.send("acquire 0 3 0");
    CHECK(contended.code == timeout_code);
    CHECK(contended.acquire_ms < 250.0);
    REQUIRE(p.a.send("release").code == 0);
    const auto no_new = p.a.send("acquire 0 3 0");
    CHECK(no_new.code == timeout_code);
    CHECK(no_new.acquire_ms < 250.0);
}

TEST_CASE("D3D11 native writable GPU pixels survive cross-process publication",
          "[.][integration][cross_process][d3d11_fanout]") {
    peers p(1);
    require_publication(p.sender.send("publish_native 11 0"));
    check_frame(p.a.send("acquire 100 11 0"));
}

TEST_CASE("D3D11 cross-process held ring slots are excluded before producer writes",
          "[.][integration][cross_process][d3d11_fanout]") {
    for (const uint32_t ring : {1u, 3u}) {
        INFO("ring=" << ring);
        peers p(ring);
        const auto initial = p.publish(4);
        require_publication(initial);
        const auto held = p.a.send("acquire 100 4 1");
        REQUIRE(held.code == 0);
        REQUIRE(held.pixels_ok);
        if (ring == 1) {
            // Probe does not write even on the broken baseline: distinguish
            // acquire-time rejection from a late commit-time timeout safely.
            const auto blocked = p.sender.send("probe");
            CHECK(blocked.acquire_code == timeout_code);
            CHECK(blocked.acquire_ms < 250.0);
            CHECK_FALSE(blocked.wrote);
        } else {
            // Three publications force wraparound to the held slot. The actor
            // refuses to write that resource if broken acquire returns it.
            for (uint32_t seed = 5; seed <= 7; ++seed) {
                const auto next = p.publish(seed, initial.resource);
                INFO("publication=" << next.encode());
                CHECK(next.acquire_code == 0);
                CHECK(next.resource != initial.resource);
                CHECK(next.code == 0);
                CHECK(next.commit_code == 0);
                CHECK(next.wrote);
                CHECK(next.acquire_ms < 250.0);
                CHECK(next.commit_ms < 250.0);
                CHECK(next.total_ms < 250.0);
                if (next.code == 0) check_frame(p.b.send("acquire 100 " + std::to_string(seed) + " 0"));
                const auto stable = p.a.send("verify 4");
                CHECK(stable.pixels_ok);
                CHECK(stable.frame == held.frame);
            }
        }
        REQUIRE(p.a.send("release").code == 0);
        require_publication(p.publish(8));
        check_frame(p.b.send("acquire 100 8 0"));
    }
}

TEST_CASE("D3D11 cross-process killed receiver abandons a resource without wedging peers",
          "[.][integration][cross_process][d3d11_fanout]") {
    peers p(1);
    const auto initial = p.publish(9);
    require_publication(initial);
    const auto held = p.a.send("acquire 100 9 1");
    REQUIRE(held.code == 0);
    REQUIRE(held.pixels_ok);
    p.a.kill();
    // A dead consumer must not turn B's session into a fatal backend error
    // while the producer is idle and has not yet replaced the abandoned slot.
    const auto unavailable = p.b.send("acquire 0 9 0");
    CHECK(unavailable.code == static_cast<int>(nozzle::ErrorCode::Timeout));
    CHECK(unavailable.acquire_ms < 250.0);
    reply recovered;
    const auto start = clock::now();
    // Recovery may invalidate on one call and allocate on the next. Never
    // write the abandoned COM identity, even if baseline acquire succeeds.
    for (int attempt = 0; attempt < 10; ++attempt) {
        recovered = p.publish(10, initial.resource);
        if (recovered.code == 0) break;
        if (fanout_test::milliseconds(start) >= 2000) break;
        Sleep(10);
    }
    INFO("abandonment recovery=" << recovered.encode());
    CHECK(recovered.code == 0);
    CHECK(recovered.resource != initial.resource);
    CHECK(fanout_test::milliseconds(start) < 2000.0);
    if (recovered.code == 0) {
        CHECK(recovered.wrote);
        CHECK(recovered.commit_code == 0);
        const auto received = p.b.send("acquire 100 10 0");
        check_frame(received);
        if (received.code == 0) CHECK(received.frame > held.frame);
    }
}
