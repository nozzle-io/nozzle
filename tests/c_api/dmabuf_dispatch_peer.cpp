// nozzle - dmabuf_dispatch_peer.cpp - Independent dispatch header consumer

#include "dmabuf_dispatch_peer.hpp"
#include <nozzle/config.hpp>

#if NOZZLE_HAS_DMA_BUF && NOZZLE_ENABLE_TEST_HOOKS
#include "backends/backend_dispatch.hpp"

namespace dmabuf_dispatch_peer {

const void *sender_state_address() {
    return &nozzle::detail::backend::g_sender_state;
}

const void *receiver_cache_address() {
    return &nozzle::detail::backend::get_receiver_cache();
}

int sender_slot_fd(uint32_t slot) {
    // Read this TU's state directly: a coalesced inline test getter could hide ODR bugs.
    auto *state = nozzle::detail::backend::g_sender_state.load(std::memory_order_relaxed);
    if (!state || nozzle::detail::kMaxRingSlots <= slot) {
        return -1;
    }
    std::lock_guard<std::mutex> lock(state->mutex_);
    return state->slot_fds_[slot];
}

} // namespace dmabuf_dispatch_peer
#endif
