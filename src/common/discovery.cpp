// nozzle - discovery.cpp - Sender discovery for receivers

#include <nozzle/config.hpp>
#include <nozzle/discovery.hpp>
#include <nozzle/types.hpp>

#include "ipc.hpp"
#include "registry.hpp"
#include "shared_state.hpp"
#include "shared_layout.hpp"

#include <cstring>
#include <exception>
#include <vector>

namespace nozzle {

std::vector<sender_info> enumerate_senders() {
    std::vector<sender_info> result;

#if NOZZLE_HAS_EXCEPTIONS
    try {
#endif

    auto snapshot = detail::registry::read_directory_snapshot();
    if (!snapshot.ok()) return result;

    for (uint32_t i = 0; i < snapshot.value().capacity; ++i) {
        const auto &entry = snapshot.value().entries[i];
        if (entry.valid != detail::entry_valid_flag::valid ||
            !detail::valid_directory_strings(entry)) {
            continue;
        }

        if (entry.pid == detail::kInvalidPid) {
            continue;
        }

        if (!detail::ipc::is_pid_alive(entry.pid)) {
            continue;
        }

        sender_info info{};
        info.name = entry.sender_name;
        info.application_name = entry.application_name;
        info.id = entry.uuid;
        info.backend = static_cast<backend_type>(entry.backend);
        result.push_back(std::move(info));
    }

#if NOZZLE_HAS_EXCEPTIONS
    } catch (const std::exception &) {
        result.clear();
    }
#endif

    return result;
}

} // namespace nozzle
