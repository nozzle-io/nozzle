// nozzle - discovery.cpp - Sender discovery for receivers

#include <bbb/nozzle/discovery.hpp>
#include <bbb/nozzle/types.hpp>

#include "shared_state.hpp"

#include <cstring>
#include <vector>

#include <fcntl.h>
#include <signal.h>
#include <sys/mman.h>
#include <unistd.h>

namespace bbb {
namespace nozzle {

std::vector<sender_info> enumerate_senders() {
    std::vector<sender_info> result;

    int fd = shm_open(detail::kDirectoryShmName, O_RDONLY, 0);
    if (fd < 0) {
        return result;
    }

    const auto total_size = sizeof(detail::DirectoryHeader) +
                            sizeof(detail::DirectoryEntry) * detail::kMaxSenders;

    void *mapped = mmap(nullptr, total_size, PROT_READ, MAP_SHARED, fd, 0);
    if (mapped == MAP_FAILED) {
        close(fd);
        return result;
    }

    auto *header = static_cast<const detail::DirectoryHeader *>(mapped);
    if (header->magic != detail::kDirectoryMagic) {
        munmap(mapped, total_size);
        close(fd);
        return result;
    }

    auto *entries = reinterpret_cast<const detail::DirectoryEntry *>(
        static_cast<const uint8_t *>(mapped) + sizeof(detail::DirectoryHeader));

    for (uint32_t i = 0; i < header->capacity; ++i) {
        if (entries[i].valid != 1) {
            continue;
        }

        if (entries[i].pid <= 0) {
            continue;
        }

        if (kill(entries[i].pid, 0) != 0 && errno == ESRCH) {
            continue;
        }

        sender_info info{};
        info.name = entries[i].sender_name;
        info.application_name = entries[i].application_name;
        info.id = entries[i].uuid;
        info.backend = static_cast<backend_type>(entries[i].backend);
        result.push_back(std::move(info));
    }

    munmap(mapped, total_size);
    close(fd);
    return result;
}

} // namespace nozzle
} // namespace bbb
