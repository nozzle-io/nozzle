// nozzle - registry.cpp - Sender/receiver registration and shared registry management

#include "registry.hpp"

#include <nozzle/result.hpp>

#include <cstdio>
#include <cstring>
#include <mutex>

#include "ipc.hpp"
#include "name_validation.hpp"
#include "shared_layout.hpp"
#include "shared_state.hpp"

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
    uint32_t capacity{0};

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
        , capacity{other.capacity}
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
            capacity = other.capacity;
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

        auto prefix = validate_mapping_prefix(dir.handle, kDirectoryMagic, "directory");
        if (!prefix.ok()) return prefix.error();
        auto map_result = ipc::shm_map(dir.handle, total_size, true);
        if (!map_result.ok()) {
            return map_result.error();
        }
        dir.mapped = map_result.value();
        dir.mapped_size = total_size;
    } else {
        bool created = false;
        auto shm_result = ipc::shm_create(kDirectoryShmName, total_size, &created);
        if (!shm_result.ok()) {
            return shm_result.error();
        }
        dir.handle = std::move(shm_result.value());

        if (!created) {
            auto prefix = validate_mapping_prefix(dir.handle, kDirectoryMagic, "directory");
            if (!prefix.ok()) return prefix.error();
        }
        auto map_result = ipc::shm_map(dir.handle, total_size, false);
        if (!map_result.ok()) {
            if (created) ipc::shm_unlink(kDirectoryShmName);
            return map_result.error();
        }
        dir.mapped = map_result.value();
        dir.mapped_size = total_size;

        auto *header = static_cast<DirectoryHeader *>(dir.mapped);
        if (created) {
            std::memset(dir.mapped, 0, total_size);
            header->magic = kDirectoryMagic;
            header->version = kSharedMemVersion;
            header->capacity = kMaxSenders;
        }
    }

    auto capacity = validate_directory_header(*static_cast<const DirectoryHeader *>(dir.mapped));
    if (!capacity.ok()) return capacity.error();
    dir.capacity = capacity.value();
    const auto *entries = reinterpret_cast<const DirectoryEntry *>(
        static_cast<const uint8_t *>(dir.mapped) + sizeof(DirectoryHeader));
    for (uint32_t i = 0; i < dir.capacity; ++i) {
        if (entries[i].valid == entry_valid_flag::valid && !valid_directory_strings(entries[i])) {
            return Error{ErrorCode::BackendError, "nozzle directory has unterminated entry fields"};
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

    for (uint32_t i = 0; i < dir.capacity; ++i) {
        if (entries[i].valid == entry_valid_flag::valid && !ipc::is_pid_alive(entries[i].pid)) {
            entries[i].valid = entry_valid_flag::invalid;
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

    const std::string_view sender_name{name};
    const std::string_view application_name{app_name};
    auto name_result = validate_sender_name(sender_name);
    if (!name_result.ok()) return name_result.error();
    auto app_result = validate_application_name(application_name);
    if (!app_result.ok()) return app_result.error();
    const auto application_length = utf8_prefix_length(
        application_name, sizeof(SenderSharedState::application_name) - 1);
    static_assert(sizeof(DirectoryEntry::sender_name) == sizeof(SenderSharedState::name), "name capacity mismatch");
    static_assert(sizeof(DirectoryEntry::application_name) == sizeof(SenderSharedState::application_name), "application capacity mismatch");

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

    for (uint32_t i = 0; i < dir.capacity; ++i) {
        if (entries[i].valid == entry_valid_flag::valid && !ipc::is_pid_alive(entries[i].pid)) {
            entries[i].valid = entry_valid_flag::invalid;
        }
    }

    int slot_index = kSlotNotFound;
    for (uint32_t i = 0; i < dir.capacity; ++i) {
        if (entries[i].valid == entry_valid_flag::invalid) {
            slot_index = static_cast<int>(i);
            break;
        }
    }
    if (slot_index == kSlotNotFound) {
        return Error{ErrorCode::ResourceCreationFailed, "no available sender slots"};
    }

    auto &entry = entries[slot_index];
    std::memset(&entry, 0, sizeof(entry));
    std::memcpy(entry.sender_name, name, sender_name.size());
    std::memcpy(entry.application_name, app_name, application_length);
    std::memcpy(entry.uuid, reg.uuid, sizeof(entry.uuid));
    std::memcpy(entry.shm_name, reg.shm_name, sizeof(entry.shm_name));
    entry.backend = backend;
    entry.pid = ipc::current_pid();
    entry.valid = entry_valid_flag::valid;

    ipc::atomic_store_relaxed(&header->change_counter,
        ipc::atomic_load_relaxed(&header->change_counter) + 1);

    bool created = false;
    auto sender_shm = ipc::shm_create(reg.shm_name, sizeof(SenderSharedState), &created);
    if (!sender_shm.ok()) {
        entry.valid = entry_valid_flag::invalid;
        return Error{ErrorCode::ResourceCreationFailed, "shm_create failed for sender state"};
    }
    if (!created) {
        ipc::shm_close(sender_shm.value());
        entry.valid = entry_valid_flag::invalid;
        return Error{ErrorCode::ResourceCreationFailed, "sender state name already exists"};
    }

    auto sender_map = ipc::shm_map(sender_shm.value(), sizeof(SenderSharedState), false);
    if (!sender_map.ok()) {
        ipc::shm_close(sender_shm.value());
        ipc::shm_unlink(reg.shm_name);
        entry.valid = entry_valid_flag::invalid;
        return Error{ErrorCode::ResourceCreationFailed, "shm_map failed for sender state"};
    }

    auto *state = static_cast<SenderSharedState *>(sender_map.value());
    std::memset(state, 0, sizeof(SenderSharedState));
    state->magic = kSenderMagic;
    state->version = kSharedMemVersion;
    std::memcpy(state->name, name, sender_name.size());
    std::memcpy(state->application_name, app_name, application_length);
    std::memcpy(state->uuid, reg.uuid, sizeof(state->uuid));
    state->backend = backend;
    state->width = width;
    state->height = height;
    state->format = format;
    state->ring_size = ring_size < kMinimumRingSize ? kMinimumRingSize : (ring_size > kMaxRingSlots ? kMaxRingSlots : ring_size);

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

    for (uint32_t i = 0; i < dir.capacity; ++i) {
        if (entries[i].valid == entry_valid_flag::valid && std::strncmp(entries[i].uuid, uuid, 36) == 0) {
            std::memcpy(shm_name, entries[i].shm_name, sizeof(shm_name));
            entries[i].valid = entry_valid_flag::invalid;
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

    auto prefix = validate_mapping_prefix(view.handle, kSenderMagic, "sender");
    if (!prefix.ok()) {
        close_sender_state(view);
        return prefix.error();
    }
    view.mapped_size = sizeof(SenderSharedState);
    auto map_result = ipc::shm_map(view.handle, view.mapped_size, true);
    if (!map_result.ok()) {
        ipc::shm_close(view.handle);
        view.handle = {};
        return map_result.error();
    }
    view.mapped = map_result.value();

    view.state = static_cast<const SenderSharedState *>(view.mapped);

    auto layout = validate_sender_layout(*view.state);
    if (!layout.ok()) {
        close_sender_state(view);
        return layout.error();
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
