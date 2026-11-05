// nozzle - dmabuf_dispatch_peer.hpp - Cross-translation-unit state probes

#pragma once

#include <cstdint>

namespace dmabuf_dispatch_peer {

const void *sender_state_address();
const void *receiver_cache_address();
int sender_slot_fd(uint32_t slot);

} // namespace dmabuf_dispatch_peer
