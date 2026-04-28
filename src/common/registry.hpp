#pragma once

#include <bbb/nozzle/result.hpp>

#include "ipc.hpp"
#include "shared_state.hpp"

#include <cstddef>

namespace bbb {
namespace nozzle {
namespace detail {
namespace registry {

struct Registration {
    char uuid[37]{};
    char shm_name[96]{};
};

struct SenderStateView {
    const SenderSharedState *state{nullptr};
    ipc::shm_handle handle{};
    void *mapped{nullptr};
    std::size_t mapped_size{0};
};

Result<Registration> register_sender(
    const char *name,
    const char *app_name,
    uint8_t backend,
    uint32_t width,
    uint32_t height,
    uint32_t format,
    uint32_t ring_size);

Result<void> unregister_sender(const char *uuid);

void cleanup_stale_entries();

Result<SenderStateView> open_sender_state(const char *shm_name);

void close_sender_state(SenderStateView &view);

} // namespace registry
} // namespace detail
} // namespace nozzle
} // namespace bbb
