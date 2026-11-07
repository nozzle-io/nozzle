// nozzle - discovery.cpp - Sender discovery for receivers

#include <nozzle/config.hpp>
#include <nozzle/discovery.hpp>
#include <nozzle/types.hpp>

#include "ipc.hpp"
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

    auto shm_result = detail::ipc::shm_open_ro(detail::kDirectoryShmName);
    if (!shm_result.ok()) {
        return result;
    }

    auto prefix = detail::validate_mapping_prefix(shm_result.value(), detail::kDirectoryMagic, "directory");
    if (!prefix.ok()) {
        detail::ipc::shm_close(shm_result.value());
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
    auto capacity = detail::validate_directory_header(*header);
    if (!capacity.ok()) {
        detail::ipc::shm_unmap(mapped, total_size);
        detail::ipc::shm_close(shm_result.value());
        return result;
    }

    auto *entries = reinterpret_cast<const detail::DirectoryEntry *>(
        static_cast<const uint8_t *>(mapped) + sizeof(detail::DirectoryHeader));

    for (uint32_t i = 0; i < capacity.value(); ++i) {
        const auto entry = entries[i];
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

    detail::ipc::shm_unmap(mapped, total_size);
    detail::ipc::shm_close(shm_result.value());

#if NOZZLE_HAS_EXCEPTIONS
    } catch (const std::exception &) {
        result.clear();
    }
#endif

    return result;
}

} // namespace nozzle
