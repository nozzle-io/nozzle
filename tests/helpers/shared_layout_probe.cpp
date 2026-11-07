#include "ipc.hpp"
#include "registry.hpp"
#include "shared_state.hpp"

#include <nozzle/discovery.hpp>
#include <nozzle/receiver.hpp>

#include <cstdio>
#include <cstring>
#include <string>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <sys/resource.h>
#endif

namespace ipc = nozzle::detail::ipc;
namespace registry = nozzle::detail::registry;
namespace nd = nozzle::detail;

namespace {
// 10 is a behavioral mismatch, 20 a setup failure, 127 an invocation failure.
template <typename T>
int expect_rejection(const nozzle::Result<T> &result, bool version, bool layout_error = true) {
    if (result.ok()) {
        std::fprintf(stderr, "FAIL: malformed shared layout was accepted\n");
        return 10;
    }
    std::fprintf(stderr, "error=%d message=%s\n",
        static_cast<int>(result.error().code), result.error().message.c_str());
    if (layout_error && result.error().code != nozzle::ErrorCode::BackendError) return 10;
    if (version && result.error().message.find("version") == std::string::npos) return 10;
    return 0;
}
} // namespace

int main(int argc, char **argv) {
#if defined(_WIN32)
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
#else
    const rlimit no_core{0, 0};
    if (setrlimit(RLIMIT_CORE, &no_core) != 0) return 20;
#endif
    if (argc != 4) return 127;
    const std::string operation = argv[1];
    const char *name = argv[2];
    const char *state_name = argv[3];
    const bool version = operation.find("version") != std::string::npos;
    std::fprintf(stderr, "shared_layout probe: %s\n", operation.c_str());
    std::fflush(stderr);

    if (operation == "enumerate") {
        return nozzle::enumerate_senders().empty() ? 0 : 10;
    }
    if (operation == "register" || operation == "register_version" || operation == "register_short") {
        auto registered = registry::register_sender("layout_probe_extra", "layout_tests",
            0, 8, 8, static_cast<uint32_t>(nozzle::texture_format::rgba8_unorm), 2);
        // Never unregister through a deliberately corrupt directory.
        if (registered.ok()) ipc::shm_unlink(registered.value().shm_name);
        return expect_rejection(registered, version, operation != "register_short");
    }
    if (operation == "map_state") {
        auto opened = ipc::shm_open_ro(state_name);
        if (!opened.ok()) return 20;
        auto mapped = ipc::shm_map(opened.value(), sizeof(nd::SenderSharedState), true);
        const int status = expect_rejection(mapped, false, false);
        if (mapped.ok()) ipc::shm_unmap(mapped.value(), sizeof(nd::SenderSharedState));
        ipc::shm_close(opened.value());
        return status;
    }
    if (operation == "open_state" || operation == "open_state_version") {
        auto opened = registry::open_sender_state(state_name);
        const int status = expect_rejection(opened, version, version);
        if (opened.ok()) registry::close_sender_state(opened.value());
        return status;
    }

    nozzle::receiver_desc desc{};
    desc.name = name;
    auto connected = nozzle::receiver::create(desc);
    if (operation == "connect" || operation == "connect_version"
        || operation == "connect_short" || operation == "connect_state_short" || operation == "connect_state_version") {
        return expect_rejection(connected, version, operation == "connect" || version);
    }
    if (!connected.ok()) {
        std::fprintf(stderr, "SETUP: receiver connection failed: %s\n", connected.error().message.c_str());
        return 20;
    }
    if (operation == "control") {
        const auto info = connected.value().connected_info();
        return info.name == name && info.format == nozzle::texture_format::rgba8_unorm
            && info.width == 8 && info.height == 8 ? 0 : 10;
    }
    if (operation != "ring_zero" && operation != "ring_large" && operation != "slot_invalid") return 127;

    auto opened = ipc::shm_open_rw(state_name);
    if (!opened.ok()) return 20;
    auto mapped = ipc::shm_map(opened.value(), sizeof(nd::SenderSharedState), false);
    if (!mapped.ok()) {
        ipc::shm_close(opened.value());
        return 20;
    }
    auto *state = static_cast<nd::SenderSharedState *>(mapped.value());
    if (operation == "ring_zero") state->ring_size = 0;
    if (operation == "ring_large") state->ring_size = nd::kMaxRingSlots + 1;
    // Empty slot metadata guarantees the baseline returns Timeout instead of
    // reaching a GPU backend, except ring_zero which faults in the modulo.
    ipc::atomic_store_release_32(&state->committed_slot,
        operation == "slot_invalid" ? state->ring_size : 0);
    ipc::atomic_store_release_64(&state->committed_frame, 1);
    nozzle::acquire_desc acquire{};
    acquire.timeout_ms = 0;
    auto frame = connected.value().acquire_frame(acquire);
    ipc::shm_unmap(mapped.value(), sizeof(nd::SenderSharedState));
    ipc::shm_close(opened.value());
    if (frame.ok()) return 10;
    std::fprintf(stderr, "acquire error=%d message=%s\n",
        static_cast<int>(frame.error().code), frame.error().message.c_str());
    return frame.error().code == nozzle::ErrorCode::BackendError ? 0 : 10;
}
