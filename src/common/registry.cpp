// nozzle - registry.cpp - Sender/receiver registration and shared registry management

#include "registry.hpp"

#include <bbb/nozzle/result.hpp>

#include <cstdio>
#include <cstring>
#include <mutex>

#include "ipc.hpp"
#include "shared_state.hpp"

namespace bbb {
namespace nozzle {
namespace detail {
namespace registry {

namespace {

std::mutex process_mutex_;

void generate_uuid(char *out) {
    uint8_t bytes[16]{};
    ipc::random_bytes(bytes, sizeof(bytes));
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
    ipc::dir_lock lock{};
    ipc::shm_handle handle{};
    void *mapped{nullptr};
    std::size_t mapped_size{0};

    ~ScopedDirLock() {
        if (mapped != nullptr) {
            ipc::shm_unmap(mapped, mapped_size);
        }
        ipc::shm_close(handle);
        ipc::dir_lock_release(lock);
    }

    ScopedDirLock() = default;

    ScopedDirLock(const ScopedDirLock &) = delete;
    ScopedDirLock &operator=(const ScopedDirLock &) = delete;

    ScopedDirLock(ScopedDirLock &&other) noexcept
        : lock{other.lock}
        , handle{other.handle}
        , mapped{other.mapped}
        , mapped_size{other.mapped_size}
    {
        other.lock = {};
        other.handle = {};
        other.mapped = nullptr;
        other.mapped_size = 0;
    }
    ScopedDirLock &operator=(ScopedDirLock &&other) noexcept {
        if (this != &other) {
            lock = other.lock;
            handle = other.handle;
            mapped = other.mapped;
            mapped_size = other.mapped_size;
            other.lock = {};
            other.handle = {};
            other.mapped = nullptr;
            other.mapped_size = 0;
        }
        return *this;
    }
};

Result<ScopedDirLock> open_directory_locked(bool read_only) {
    ScopedDirLock dir{};
    const auto total_size = sizeof(DirectoryHeader) + sizeof(DirectoryEntry) * kMaxSenders;

    auto lock_result = ipc::dir_lock_acquire(kDirectoryShmName, !read_only);
    if (!lock_result.ok()) {
        return lock_result.error();
    }
    dir.lock = std::move(lock_result.value());

    if (read_only) {
        auto shm_result = ipc::shm_open_ro(kDirectoryShmName);
        if (!shm_result.ok()) {
            return shm_result.error();
        }
        dir.handle = std::move(shm_result.value());

        auto map_result = ipc::shm_map(dir.handle, total_size, true);
        if (!map_result.ok()) {
            return map_result.error();
        }
        dir.mapped = map_result.value();
        dir.mapped_size = total_size;
    } else {
        auto shm_result = ipc::shm_create(kDirectoryShmName, total_size);
        if (!shm_result.ok()) {
            return shm_result.error();
        }
        dir.handle = std::move(shm_result.value());

        auto map_result = ipc::shm_map(dir.handle, total_size, false);
        if (!map_result.ok()) {
            return map_result.error();
        }
        dir.mapped = map_result.value();
        dir.mapped_size = total_size;

        auto *header = static_cast<DirectoryHeader *>(dir.mapped);
        if (header->magic != kDirectoryMagic) {
            std::memset(dir.mapped, 0, total_size);
            header->magic = kDirectoryMagic;
            header->version = kSharedMemVersion;
            header->capacity = kMaxSenders;
        }
    }

    return dir;
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
        if (entries[i].valid == 1 && !ipc::is_pid_alive(entries[i].pid)) {
            entries[i].valid = 0;
        }
    }

    ipc::atomic_store_relaxed(&header->change_counter,
        ipc::atomic_load_relaxed(&header->change_counter) + 1);
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
        if (entries[i].valid == 1 && !ipc::is_pid_alive(entries[i].pid)) {
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
    entry.pid = ipc::current_pid();
    entry.valid = 1;

    ipc::atomic_store_relaxed(&header->change_counter,
        ipc::atomic_load_relaxed(&header->change_counter) + 1);

    auto sender_shm = ipc::shm_create(reg.shm_name, sizeof(SenderSharedState));
    if (!sender_shm.ok()) {
        entry.valid = 0;
        return Error{ErrorCode::ResourceCreationFailed, "shm_create failed for sender state"};
    }

    auto sender_map = ipc::shm_map(sender_shm.value(), sizeof(SenderSharedState), false);
    if (!sender_map.ok()) {
        ipc::shm_close(sender_shm.value());
        ipc::shm_unlink(reg.shm_name);
        entry.valid = 0;
        return Error{ErrorCode::ResourceCreationFailed, "shm_map failed for sender state"};
    }

    auto *state = static_cast<SenderSharedState *>(sender_map.value());
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

    ipc::shm_unmap(sender_map.value(), sizeof(SenderSharedState));
    ipc::shm_close(sender_shm.value());

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

    ipc::atomic_store_relaxed(&header->change_counter,
        ipc::atomic_load_relaxed(&header->change_counter) + 1);

    ipc::shm_unlink(shm_name);

    return {};
}

Result<SenderStateView> open_sender_state(const char *shm_name) {
    SenderStateView view{};

    auto shm_result = ipc::shm_open_ro(shm_name);
    if (!shm_result.ok()) {
        return shm_result.error();
    }
    view.handle = std::move(shm_result.value());

    view.mapped_size = sizeof(SenderSharedState);
    auto map_result = ipc::shm_map(view.handle, view.mapped_size, true);
    if (!map_result.ok()) {
        ipc::shm_close(view.handle);
        view.handle = {};
        return map_result.error();
    }
    view.mapped = map_result.value();

    view.state = static_cast<const SenderSharedState *>(view.mapped);

    if (view.state->magic != kSenderMagic) {
        close_sender_state(view);
        return Error{ErrorCode::BackendError, "sender state has invalid magic"};
    }

    return view;
}

void close_sender_state(SenderStateView &view) {
    if (view.mapped != nullptr) {
        ipc::shm_unmap(view.mapped, view.mapped_size);
        view.mapped = nullptr;
    }
    ipc::shm_close(view.handle);
    view.state = nullptr;
    view.mapped_size = 0;
}

} // namespace registry
} // namespace detail
} // namespace nozzle
} // namespace bbb
