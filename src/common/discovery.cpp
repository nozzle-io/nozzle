// nozzle - discovery.cpp - Sender discovery for receivers

#include <nozzle/discovery.hpp>
#include <nozzle/types.hpp>

#include "ipc.hpp"
#include "shared_state.hpp"

#include <cstring>
#include <vector>

namespace nozzle {

std::vector<sender_info> enumerate_senders() {
    std::vector<sender_info> result;

    auto shm_result = detail::ipc::shm_open_ro(detail::kDirectoryShmName);
    if (!shm_result.ok()) {
        return result;
    }

    const auto total_size = sizeof(detail::DirectoryHeader) +
                            sizeof(detail::DirectoryEntry) * detail::kMaxSenders;

    auto map_result = detail::ipc::shm_map(shm_result.value(), total_size, true);
    if (!map_result.ok()) {
        detail::ipc::shm_close(shm_result.value());
        return result;
    }
    void *mapped = map_result.value();

    auto *header = static_cast<const detail::DirectoryHeader *>(mapped);
    if (header->magic != detail::kDirectoryMagic) {
        detail::ipc::shm_unmap(mapped, total_size);
        detail::ipc::shm_close(shm_result.value());
        return result;
    }

    auto *entries = reinterpret_cast<const detail::DirectoryEntry *>(
        static_cast<const uint8_t *>(mapped) + sizeof(detail::DirectoryHeader));

    for (uint32_t i = 0; i < header->capacity; ++i) {
        if (entries[i].valid != 1) {
            continue;
        }

        if (entries[i].pid == 0) {
            continue;
        }

        if (!detail::ipc::is_pid_alive(entries[i].pid)) {
            continue;
        }

        sender_info info{};
        info.name = entries[i].sender_name;
        info.application_name = entries[i].application_name;
        info.id = entries[i].uuid;
        info.backend = static_cast<backend_type>(entries[i].backend);
        result.push_back(std::move(info));
    }

    detail::ipc::shm_unmap(mapped, total_size);
    detail::ipc::shm_close(shm_result.value());
    return result;
}

} // namespace nozzle
