// nozzle - registry.cpp - Sender/receiver registration and shared registry management

#include "registry.hpp"

#include <bbb/nozzle/result.hpp>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>

#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace bbb {
namespace nozzle {
namespace detail {
namespace registry {

namespace {

std::mutex process_mutex_;

static void generate_uuid(char *out) {
    uint8_t bytes[16]{};
    arc4random_buf(bytes, sizeof(bytes));
    bytes[6] = (bytes[6] & 0x0F) | 0x40;
    bytes[8] = (bytes[8] & 0x3F) | 0x80;
    std::snprintf(
        out, 37,
        "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
        bytes[0], bytes[1], bytes[2], bytes[3],
        bytes[4], bytes[5], bytes[6], bytes[7],
        bytes[8], bytes[9], bytes[10], bytes[11],
        bytes[12], bytes[13], bytes[14], bytes[15]);
}

struct ScopedDirLock {
    int fd{-1};
    void *mapped{nullptr};
    std::size_t mapped_size{0};
    bool locked{false};

    ~ScopedDirLock() {
        if (locked && fd >= 0) {
            struct flock fl{};
            fl.l_type = F_UNLCK;
            fl.l_whence = SEEK_SET;
            fl.l_start = 0;
            fl.l_len = 0;
            fcntl(fd, F_SETLK, &fl);
        }
        if (mapped != nullptr) {
            munmap(mapped, mapped_size);
        }
        if (fd >= 0) {
            close(fd);
        }
    }

    ScopedDirLock() = default;

    ScopedDirLock(const ScopedDirLock &) = delete;
    ScopedDirLock &operator=(const ScopedDirLock &) = delete;
    ScopedDirLock(ScopedDirLock &&other) noexcept
        : fd{other.fd}
        , mapped{other.mapped}
        , mapped_size{other.mapped_size}
        , locked{other.locked}
    {
        other.fd = -1;
        other.mapped = nullptr;
        other.mapped_size = 0;
        other.locked = false;
    }
    ScopedDirLock &operator=(ScopedDirLock &&other) noexcept {
        if (this != &other) {
            fd = other.fd;
            mapped = other.mapped;
            mapped_size = other.mapped_size;
            locked = other.locked;
            other.fd = -1;
            other.mapped = nullptr;
            other.mapped_size = 0;
            other.locked = false;
        }
        return *this;
    }
};

Result<ScopedDirLock> open_directory_locked(bool read_only) {
    ScopedDirLock dir{};

    const int flags = read_only ? O_RDONLY : O_RDWR | O_CREAT;
    const mode_t mode = 0666;
    dir.fd = shm_open(kDirectoryShmName, flags, mode);
    if (dir.fd < 0) {
        return Error{ErrorCode::ResourceCreationFailed, "shm_open failed for directory"};
    }

    const auto total_size = sizeof(DirectoryHeader) + sizeof(DirectoryEntry) * kMaxSenders;
    dir.mapped_size = total_size;

    if (!read_only) {
        struct stat st{};
        bool needs_truncate = true;
        if (fstat(dir.fd, &st) == 0 && static_cast<std::size_t>(st.st_size) >= total_size) {
            needs_truncate = false;
        }
        if (needs_truncate) {
            if (ftruncate(dir.fd, static_cast<off_t>(total_size)) != 0) {
                auto saved_errno = errno;
                // Segment may be corrupt — destroy and recreate
                close(dir.fd);
                dir.fd = -1;
                shm_unlink(kDirectoryShmName);
                dir.fd = shm_open(kDirectoryShmName, flags, mode);
                if (dir.fd < 0) {
                    return Error{ErrorCode::ResourceCreationFailed, "shm_open recreate failed for directory"};
                }
                if (ftruncate(dir.fd, static_cast<off_t>(total_size)) != 0) {
                    auto saved2 = errno;
                    char buf[160]{};
                    std::snprintf(buf, sizeof(buf),
                                  "ftruncate failed for directory: %s (errno=%d, size=%zu, retry_errno=%d)",
                                  std::strerror(saved_errno), saved_errno, total_size, saved2);
                    close(dir.fd);
                    dir.fd = -1;
                    return Error{ErrorCode::ResourceCreationFailed, buf};
                }
            }
        }
    }

    const int prot = read_only ? PROT_READ : PROT_READ | PROT_WRITE;
    dir.mapped = mmap(nullptr, total_size, prot, MAP_SHARED, dir.fd, 0);
    if (dir.mapped == MAP_FAILED) {
        dir.mapped = nullptr;
        return Error{ErrorCode::ResourceCreationFailed, "mmap failed for directory"};
    }

    if (!read_only) {
        auto *header = static_cast<DirectoryHeader *>(dir.mapped);
        if (header->magic != kDirectoryMagic) {
            std::memset(dir.mapped, 0, total_size);
            header->magic = kDirectoryMagic;
            header->version = kSharedMemVersion;
            header->capacity = kMaxSenders;
        }

        struct flock fl{};
        fl.l_type = F_WRLCK;
        fl.l_whence = SEEK_SET;
        fl.l_start = 0;
        fl.l_len = 0;
        if (fcntl(dir.fd, F_SETLKW, &fl) == 0) {
            dir.locked = true;
        }
    }

    return dir;
}

bool is_pid_alive(pid_t pid) {
    if (pid <= 0) {
        return false;
    }
    return kill(pid, 0) == 0 || errno != ESRCH;
}

} // anonymous namespace

void cleanup_stale_entries() {
    auto dir_result = open_directory_locked(false);
    if (!dir_result.ok()) {
        return;
    }
    auto &dir = dir_result.value();

    auto *header = static_cast<DirectoryHeader *>(dir.mapped);
    auto *entries = reinterpret_cast<DirectoryEntry *>(
        static_cast<uint8_t *>(dir.mapped) + sizeof(DirectoryHeader));

    for (uint32_t i = 0; i < header->capacity; ++i) {
        if (entries[i].valid == 1 && !is_pid_alive(entries[i].pid)) {
            entries[i].valid = 0;
        }
    }

    __atomic_store_n(&header->change_counter,
                     __atomic_load_n(&header->change_counter, __ATOMIC_RELAXED) + 1,
                     __ATOMIC_RELAXED);
}

Result<Registration> register_sender(
    const char *name,
    const char *app_name,
    uint8_t backend,
    uint32_t width,
    uint32_t height,
    uint32_t format,
    uint32_t ring_size)
{
    std::lock_guard<std::mutex> lock(process_mutex_);

    if (!name || !app_name) {
        return Error{ErrorCode::InvalidArgument, "name and app_name must not be null"};
    }

    Registration reg{};

    generate_uuid(reg.uuid);
    std::snprintf(reg.shm_name, sizeof(reg.shm_name), "%s%.8s", kSenderShmPrefix, reg.uuid);

    auto dir_result = open_directory_locked(false);
    if (!dir_result.ok()) {
        return dir_result.error();
    }
    auto &dir = dir_result.value();

    auto *header = static_cast<DirectoryHeader *>(dir.mapped);
    auto *entries = reinterpret_cast<DirectoryEntry *>(
        static_cast<uint8_t *>(dir.mapped) + sizeof(DirectoryHeader));

    for (uint32_t i = 0; i < header->capacity; ++i) {
        if (entries[i].valid == 1 && !is_pid_alive(entries[i].pid)) {
            entries[i].valid = 0;
        }
    }

    int slot_index = -1;
    for (uint32_t i = 0; i < header->capacity; ++i) {
        if (entries[i].valid == 0) {
            slot_index = static_cast<int>(i);
            break;
        }
    }
    if (slot_index < 0) {
        return Error{ErrorCode::ResourceCreationFailed, "no available sender slots"};
    }

    auto &entry = entries[slot_index];
    std::memset(&entry, 0, sizeof(entry));
    std::snprintf(entry.sender_name, sizeof(entry.sender_name), "%s", name);
    std::snprintf(entry.application_name, sizeof(entry.application_name), "%s", app_name);
    std::memcpy(entry.uuid, reg.uuid, sizeof(entry.uuid));
    std::memcpy(entry.shm_name, reg.shm_name, sizeof(entry.shm_name));
    entry.backend = backend;
    entry.pid = getpid();
    entry.valid = 1;

    __atomic_store_n(&header->change_counter,
                     __atomic_load_n(&header->change_counter, __ATOMIC_RELAXED) + 1,
                     __ATOMIC_RELAXED);

    int sender_fd = shm_open(reg.shm_name, O_RDWR | O_CREAT | O_EXCL, 0666);
    if (sender_fd < 0) {
        entry.valid = 0;
        return Error{ErrorCode::ResourceCreationFailed, "shm_open failed for sender state"};
    }

    if (ftruncate(sender_fd, static_cast<off_t>(sizeof(SenderSharedState))) != 0) {
        close(sender_fd);
        shm_unlink(reg.shm_name);
        entry.valid = 0;
        return Error{ErrorCode::ResourceCreationFailed, "ftruncate failed for sender state"};
    }

    void *sender_ptr = mmap(
        nullptr, sizeof(SenderSharedState), PROT_READ | PROT_WRITE, MAP_SHARED, sender_fd, 0);
    if (sender_ptr == MAP_FAILED) {
        close(sender_fd);
        shm_unlink(reg.shm_name);
        entry.valid = 0;
        return Error{ErrorCode::ResourceCreationFailed, "mmap failed for sender state"};
    }

    auto *state = static_cast<SenderSharedState *>(sender_ptr);
    std::memset(state, 0, sizeof(SenderSharedState));
    state->magic = kSenderMagic;
    state->version = kSharedMemVersion;
    std::snprintf(state->name, sizeof(state->name), "%s", name);
    std::snprintf(state->application_name, sizeof(state->application_name), "%s", app_name);
    std::memcpy(state->uuid, reg.uuid, sizeof(state->uuid));
    state->backend = backend;
    state->width = width;
    state->height = height;
    state->format = format;
    state->ring_size = ring_size < 1 ? 1 : (ring_size > kMaxRingSlots ? kMaxRingSlots : ring_size);

    munmap(sender_ptr, sizeof(SenderSharedState));
    close(sender_fd);

    return reg;
}

Result<void> unregister_sender(const char *uuid) {
    std::lock_guard<std::mutex> lock(process_mutex_);

    auto dir_result = open_directory_locked(false);
    if (!dir_result.ok()) {
        return dir_result.error();
    }
    auto &dir = dir_result.value();

    auto *header = static_cast<DirectoryHeader *>(dir.mapped);
    auto *entries = reinterpret_cast<DirectoryEntry *>(
        static_cast<uint8_t *>(dir.mapped) + sizeof(DirectoryHeader));

    char shm_name[96]{};
    bool found = false;

    for (uint32_t i = 0; i < header->capacity; ++i) {
        if (entries[i].valid == 1 && std::strncmp(entries[i].uuid, uuid, 36) == 0) {
            std::memcpy(shm_name, entries[i].shm_name, sizeof(shm_name));
            entries[i].valid = 0;
            found = true;
            break;
        }
    }

    if (!found) {
        return Error{ErrorCode::SenderNotFound, "sender not found in directory"};
    }

    __atomic_store_n(&header->change_counter,
                     __atomic_load_n(&header->change_counter, __ATOMIC_RELAXED) + 1,
                     __ATOMIC_RELAXED);

    shm_unlink(shm_name);

    return {};
}

Result<SenderStateView> open_sender_state(const char *shm_name) {
    SenderStateView view{};

    view.fd = shm_open(shm_name, O_RDONLY, 0);
    if (view.fd < 0) {
        return Error{ErrorCode::SenderNotFound, "shm_open failed for sender state"};
    }

    view.mapped_size = sizeof(SenderSharedState);
    view.mapped = mmap(nullptr, sizeof(SenderSharedState), PROT_READ, MAP_SHARED, view.fd, 0);
    if (view.mapped == MAP_FAILED) {
        view.mapped = nullptr;
        close(view.fd);
        view.fd = -1;
        return Error{ErrorCode::ResourceCreationFailed, "mmap failed for sender state"};
    }

    view.state = static_cast<const SenderSharedState *>(view.mapped);

    if (view.state->magic != kSenderMagic) {
        close_sender_state(view);
        return Error{ErrorCode::BackendError, "sender state has invalid magic"};
    }

    return view;
}

void close_sender_state(SenderStateView &view) {
    if (view.mapped != nullptr) {
        munmap(view.mapped, view.mapped_size);
        view.mapped = nullptr;
    }
    if (view.fd >= 0) {
        close(view.fd);
        view.fd = -1;
    }
    view.state = nullptr;
    view.mapped_size = 0;
}

} // namespace registry
} // namespace detail
} // namespace nozzle
} // namespace bbb
