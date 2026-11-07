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
#include <unistd.h>
#endif

namespace ipc = nozzle::detail::ipc;
namespace registry = nozzle::detail::registry;
namespace nd = nozzle::detail;

namespace {
bool matches_state_control(const nd::SenderSharedState &state,
                           const char *name, const char *state_name) {
    return state.magic == nd::kSenderMagic && state.version == nd::kSharedMemVersion
        && state.ring_size == 2 && state.width == 8 && state.height == 8
        && state.format == static_cast<uint32_t>(nozzle::texture_format::rgba8_unorm)
        && std::strncmp(state.name, name, sizeof(state.name)) == 0
        && std::strncmp(state.application_name, "layout_tests", sizeof(state.application_name)) == 0
        && state.uuid[36] == '\0' && std::strlen(state.uuid) == 36
        && std::string(nd::kSenderShmPrefix) + std::string(state.uuid, 8) == state_name;
}

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

template <typename T>
int expect_not_found(const nozzle::Result<T> &result) {
    const int status = expect_rejection(result, false, false);
    return status == 0 && result.error().code == nozzle::ErrorCode::SenderNotFound ? 0 : 10;
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
    if (operation == "enumerate_control") {
        const auto senders = nozzle::enumerate_senders();
        return senders.size() == 1 && senders[0].name == name
            && senders[0].application_name == "layout_tests" && senders[0].id.size() == 36
            && std::string(nd::kSenderShmPrefix) + senders[0].id.substr(0, 8) == state_name ? 0 : 10;
    }
    if (operation == "register_control") {
        auto registered = registry::register_sender("layout_probe_extra", "layout_tests",
            0, 8, 8, static_cast<uint32_t>(nozzle::texture_format::rgba8_unorm), 2);
        if (!registered.ok()) {
            std::fprintf(stderr, "registration error=%d message=%s\n",
                static_cast<int>(registered.error().code), registered.error().message.c_str());
            return 10;
        }
        nozzle::receiver_desc desc{};
        desc.name = "layout_probe_extra";
        auto connected = nozzle::receiver::create(desc);
        auto opened = registry::open_sender_state(registered.value().shm_name);
        bool valid = connected.ok() && opened.ok();
        if (opened.ok()) {
            valid = valid && matches_state_control(*opened.value().state, desc.name.c_str(), registered.value().shm_name);
            registry::close_sender_state(opened.value());
        }
        if (connected.ok()) {
            const auto info = connected.value().connected_info();
            valid = valid && info.name == desc.name && info.id == registered.value().uuid;
        }
        auto directory = ipc::shm_open_ro(nd::kDirectoryShmName);
        if (directory.ok()) {
            const auto size = sizeof(nd::DirectoryHeader) + nd::kMaxSenders * sizeof(nd::DirectoryEntry);
            auto mapped = ipc::shm_map(directory.value(), size, true);
            if (mapped.ok()) {
                const auto header = *static_cast<const nd::DirectoryHeader *>(mapped.value());
                valid = valid && header.magic == nd::kDirectoryMagic
                    && header.version >= 2 && header.version <= 6 && header.capacity == nd::kMaxSenders;
                ipc::shm_unmap(mapped.value(), size);
            } else valid = false;
            ipc::shm_close(directory.value());
        } else valid = false;
        ipc::shm_unlink(registered.value().shm_name);
        return valid ? 0 : 10;
    }
    if (operation == "register" || operation == "register_version" || operation == "register_short") {
        auto registered = registry::register_sender("layout_probe_extra", "layout_tests",
            0, 8, 8, static_cast<uint32_t>(nozzle::texture_format::rgba8_unorm), 2);
        // Never unregister through a deliberately corrupt directory.
        if (registered.ok()) ipc::shm_unlink(registered.value().shm_name);
        return expect_rejection(registered, version, operation != "register_short");
    }
    if (operation == "map_directory") {
        auto opened = ipc::shm_open_ro(nd::kDirectoryShmName);
        if (!opened.ok()) return 20;
        const auto size = sizeof(nd::DirectoryHeader) + nd::kMaxSenders * sizeof(nd::DirectoryEntry);
        auto mapped = ipc::shm_map(opened.value(), size, true);
        const int status = expect_rejection(mapped, false, false);
        if (mapped.ok()) ipc::shm_unmap(mapped.value(), size);
        ipc::shm_close(opened.value());
        return status;
    }
    if (operation == "map_state" || operation == "map_state_rounded" || operation == "map_page_boundary") {
        auto opened = ipc::shm_open_ro(state_name);
        if (!opened.ok()) return 20;
        auto mapped = ipc::shm_map(opened.value(), sizeof(nd::SenderSharedState), true);
        int status = 10;
        if (operation == "map_state") {
            status = expect_rejection(mapped, false, false);
        } else if (mapped.ok()) {
            status = matches_state_control(*static_cast<const nd::SenderSharedState *>(mapped.value()),
                name, state_name) ? 0 : 10;
        }
        if (mapped.ok()) ipc::shm_unmap(mapped.value(), sizeof(nd::SenderSharedState));
        if (operation == "map_page_boundary" && status == 0) {
#if defined(_WIN32)
            SYSTEM_INFO system_info{};
            GetSystemInfo(&system_info);
            const std::size_t page_size = system_info.dwPageSize;
#else
            const long queried_size = sysconf(_SC_PAGESIZE);
            const auto page_size = queried_size > 0 ? static_cast<std::size_t>(queried_size) : 0;
#endif
            if (page_size < sizeof(nd::SenderSharedState)) {
                status = 20;
            } else {
                // The parent allocated exactly one runtime page. This request
                // exceeds backing on every OS, unlike the subpage controls.
                const auto oversized = page_size + sizeof(nd::SenderSharedState);
                std::fprintf(stderr, "page backing=%zu, normal view=%zu, oversized view=%zu\n",
                    page_size, sizeof(nd::SenderSharedState), oversized);
                auto beyond = ipc::shm_map(opened.value(), oversized, true);
                status = expect_rejection(beyond, false, false);
                if (beyond.ok()) ipc::shm_unmap(beyond.value(), oversized);
            }
        }
        ipc::shm_close(opened.value());
        return status;
    }
    if (operation == "open_state" || operation == "open_state_version" || operation == "open_state_rounded") {
        auto opened = registry::open_sender_state(state_name);
        const int status = operation == "open_state_rounded"
            ? (opened.ok() && matches_state_control(*opened.value().state, name, state_name) ? 0 : 10)
            : expect_rejection(opened, version, version);
        if (opened.ok()) registry::close_sender_state(opened.value());
        return status;
    }

    nozzle::receiver_desc desc{};
    desc.name = name;
    auto connected = nozzle::receiver::create(desc);
    if (operation == "connect_not_found") return expect_not_found(connected);
    if (operation == "connect" || operation == "connect_version"
        || operation == "connect_short" || operation == "connect_state_short" || operation == "connect_state_version") {
        return expect_rejection(connected, version, operation == "connect" || version);
    }
    if (!connected.ok()) {
        const bool acceptance_control = operation == "control" || operation == "connect_state_rounded";
        std::fprintf(stderr, "%s: receiver connection failed: %s\n",
            acceptance_control ? "FAIL" : "SETUP", connected.error().message.c_str());
        return acceptance_control ? 10 : 20;
    }
    if (operation == "control") {
        const auto info = connected.value().connected_info();
        return info.name == name && info.format == nozzle::texture_format::rgba8_unorm
            && info.width == 8 && info.height == 8 ? 0 : 10;
    }
    if (operation == "connect_state_rounded") {
        const auto info = connected.value().connected_info();
        return connected.value().is_connected() && info.name == name
            && info.application_name == "layout_tests" && info.id.size() == 36
            && std::string(nd::kSenderShmPrefix) + info.id.substr(0, 8) == state_name
            && info.format == nozzle::texture_format::rgba8_unorm
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
    // Empty slot metadata avoids GPU import when the baseline reaches it.
    // Invalid ring arithmetic can fault or continue (macOS baseline returned
    // Timeout); neither outcome satisfies the required BackendError contract.
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
