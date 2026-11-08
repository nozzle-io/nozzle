// nozzle - registry.cpp - Sender/receiver registration and shared registry management

#include "registry.hpp"

#include <nozzle/result.hpp>

#include <cstdio>
#include <cstring>
#include <mutex>

#include "ipc.hpp"
#include "metadata.hpp"
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

struct directory_mapping {
    ipc::dir_lock lock{};
    ipc::shm_handle handle{};
    void *mapped{nullptr};
    std::size_t mapped_size{0};
    uint32_t capacity{0};

    ~directory_mapping() {
        if (mapped != nullptr) {
            ipc::shm_unmap(mapped, mapped_size);
        }
        ipc::shm_close(handle);
        ipc::dir_lock_release(lock);
    }

    directory_mapping() = default;

    directory_mapping(const directory_mapping &) = delete;
    directory_mapping &operator=(const directory_mapping &) = delete;

    directory_mapping(directory_mapping &&other) noexcept
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
    directory_mapping &operator=(directory_mapping &&other) noexcept {
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

bool recoverable_directory_initialization(const void *mapped, std::size_t size) {
    const auto &header = *static_cast<const DirectoryHeader *>(mapped);
    if (header.magic != 0 || header.padding != 0 || header.change_counter != 0) return false;
    if (header.version != 0 && (header.version < kMinimumDirectoryLayoutVersion ||
        header.version > kDirectoryLayoutVersion)) return false;
    if (header.capacity != 0 && header.capacity != kMaxSenders) return false;
    const auto *bytes = static_cast<const unsigned char *>(mapped);
    for (std::size_t i = sizeof(DirectoryHeader); i < size; ++i) {
        if (bytes[i] != 0) return false;
    }
    return true;
}

Result<directory_mapping> open_directory_locked() {
    directory_mapping dir{};
    const auto total_size = sizeof(DirectoryHeader) + sizeof(DirectoryEntry) * kMaxSenders;

    auto lock_result = ipc::dir_lock_acquire(kDirectoryShmName, true);
    if (!lock_result.ok()) {
        return lock_result.error();
    }
    dir.lock = std::move(lock_result.value());

    bool created = false;
    auto shm_result = ipc::shm_create(kDirectoryShmName, total_size, &created);
    if (!shm_result.ok()) {
        return shm_result.error();
    }
    dir.handle = std::move(shm_result.value());

    bool initialize = created;
    if (!created) {
        auto extent = ipc::shm_get_size(dir.handle);
        if (extent.ok() && extent.value() == 0) {
            auto prepared = ipc::shm_initialize_empty(dir.handle, total_size);
            if (!prepared.ok()) return prepared.error();
            initialize = true;
        }
        auto prefix = validate_mapping_prefix(dir.handle, kDirectoryMagic, "directory");
        if (!prefix.ok()) {
            if (prefix.error().code != ErrorCode::SenderNotFound) return prefix.error();
            initialize = true;
        }
    }
    auto map_result = ipc::shm_map(dir.handle, total_size, false);
    if (!map_result.ok()) {
        if (created) ipc::shm_unlink(kDirectoryShmName);
        return map_result.error();
    }
    dir.mapped = map_result.value();
    dir.mapped_size = total_size;

    auto *header = static_cast<DirectoryHeader *>(dir.mapped);
    if (initialize) {
        // Both the process mutex and interprocess lock are held. A zero
        // magic with populated entries is corruption, not our startup.
        if (!created && !recoverable_directory_initialization(dir.mapped, total_size)) {
            return Error{ErrorCode::BackendError, "unpublished directory contains foreign data"};
        }
        std::memset(dir.mapped, 0, total_size);
        header->version = kDirectoryLayoutVersion;
        header->capacity = kMaxSenders;
        ipc::atomic_fence_release();
        ipc::atomic_store_release_32(&header->magic, kDirectoryMagic);
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

bool canonical_sender_name(const DirectoryEntry &entry) {
    if (!valid_directory_strings(entry) || std::strlen(entry.uuid) != 36) return false;
    for (std::size_t i = 0; i < 36; ++i) {
        const char c = entry.uuid[i];
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (c != '-') return false;
        } else if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) {
            return false;
        }
    }
    if (entry.uuid[14] != '4' || std::strchr("89ab", entry.uuid[19]) == nullptr) return false;
    char expected[sizeof(entry.shm_name)]{};
    std::snprintf(expected, sizeof(expected), "%s%.8s", kSenderShmPrefix, entry.uuid);
    return std::strcmp(expected, entry.shm_name) == 0;
}

void cleanup_stale_locked(directory_mapping &dir) {
    auto *header = static_cast<DirectoryHeader *>(dir.mapped);
    auto *entries = reinterpret_cast<DirectoryEntry *>(header + 1);
    bool changed = false;
    for (uint32_t i = 0; i < dir.capacity; ++i) {
        auto &entry = entries[i];
        if (entry.valid != entry_valid_flag::valid || !canonical_sender_name(entry)) continue;
        bool aliased = false;
        for (uint32_t j = 0; j < dir.capacity; ++j) {
            if (i != j && entries[j].valid == entry_valid_flag::valid &&
                std::strcmp(entries[j].shm_name, entry.shm_name) == 0) {
                aliased = true;
                break;
            }
        }
        if (aliased) continue;

        auto opened = open_sender_state(entry.shm_name);
        if (!opened.ok()) {
            // A missing object needs no deletion. Access/layout errors and
            // reused live PIDs are not sufficient proof to reclaim the slot.
            if (opened.error().code == ErrorCode::SenderNotFound) {
                auto owner = ipc::process_instance_token(entry.pid);
                if (!owner.ok() && owner.error().code == ErrorCode::SenderClosed) {
                    auto missing = ipc::shm_open_ro(entry.shm_name);
                    if (!missing.ok() && missing.error().code == ErrorCode::SenderNotFound) {
                        entry.valid = entry_valid_flag::invalid;
                        changed = true;
                    } else if (missing.ok()) {
                        ipc::shm_close(missing.value());
                    }
                }
            }
            continue;
        }
        auto &view = opened.value();
        bool reclaim = sender_matches_entry(*view.state, entry);
        if (reclaim) {
            auto current = owner_is_current(*view.state);
            reclaim = current.ok() && !current.value();
        }
#if !defined(_WIN32)
        if (reclaim) {
            auto owned = ipc::shm_owned_by_current_user(view.handle);
            reclaim = owned.ok() && owned.value();
        }
#endif
        if (reclaim) {
            // The directory lock excludes cooperating creators/unregisterers,
            // not hostile unlink/recreate or mutation of these identity bytes.
            auto current = owner_is_current(*view.state);
            reclaim = sender_matches_entry(*view.state, entry) && current.ok() && !current.value();
        }
#if !defined(_WIN32)
        if (reclaim) {
            const auto removed = ipc::shm_unlink_checked(entry.shm_name);
            reclaim = removed.ok();
        }
#endif
        close_sender_state(view);
        if (reclaim) {
            entry.valid = entry_valid_flag::invalid;
            changed = true;
        }
    }
    if (changed) {
        ipc::atomic_store_relaxed(&header->change_counter,
            ipc::atomic_load_relaxed(&header->change_counter) + 1);
    }
}

} // anonymous namespace

bool sender_matches_entry(const SenderSharedState &state, const DirectoryEntry &entry) {
    return valid_directory_strings(entry) && validate_sender_layout(state).ok() &&
        state.owner_pid == entry.pid && std::strcmp(state.uuid, entry.uuid) == 0 &&
        std::strcmp(state.name, entry.sender_name) == 0 &&
        std::strcmp(state.application_name, entry.application_name) == 0 &&
        state.backend == entry.backend;
}

Result<bool> owner_is_current(const SenderSharedState &state) {
    if (state.owner_instance == 0) {
        return Error{ErrorCode::BackendError, "sender has no process instance identity"};
    }
    auto actual = ipc::process_instance_token(state.owner_pid);
    if (!actual.ok()) {
        if (actual.error().code == ErrorCode::SenderClosed) return false;
        return actual.error();
    }
    return actual.value() == state.owner_instance;
}

Result<DirectorySnapshot> read_directory_snapshot() {
    // Readers never open the writer lock: render-thread polling must not wait
    // for another process or require write access to its lock file.
    directory_mapping dir{};
    auto opened = ipc::shm_open_ro(kDirectoryShmName);
    if (!opened.ok()) return opened.error();
    dir.handle = opened.value();
    auto prefix = validate_mapping_prefix(dir.handle, kDirectoryMagic, "directory");
    if (!prefix.ok()) return prefix.error();
    const auto total_size = sizeof(DirectoryHeader) + sizeof(DirectoryEntry) * kMaxSenders;
    auto mapped = ipc::shm_map(dir.handle, total_size, true);
    if (!mapped.ok()) return mapped.error();
    dir.mapped = mapped.value();
    dir.mapped_size = total_size;
    auto capacity = validate_directory_header(*static_cast<const DirectoryHeader *>(dir.mapped));
    if (!capacity.ok()) return capacity.error();
    DirectorySnapshot snapshot{};
    snapshot.capacity = capacity.value();
    const auto *entries = reinterpret_cast<const DirectoryEntry *>(
        static_cast<const uint8_t *>(dir.mapped) + sizeof(DirectoryHeader));
    std::memcpy(snapshot.entries.data(), entries, snapshot.capacity * sizeof(DirectoryEntry));
    return snapshot;
}

void cleanup_stale_entries() {
    std::lock_guard<std::mutex> lock(process_mutex_);
    auto dir_result = open_directory_locked();
    if (!dir_result.ok()) {
        return;
    }
    auto &dir = dir_result.value();

    cleanup_stale_locked(dir);
}

Result<Registration> register_sender(
    const char *name,
    const char *app_name,
    uint8_t backend,
    uint32_t width,
    uint32_t height,
    uint32_t format,
    uint32_t ring_size,
    const metadata_list &initial_metadata)
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
    const auto owner_pid = ipc::current_pid();
    auto owner_instance = ipc::process_instance_token(owner_pid);
    if (!owner_instance.ok()) return owner_instance.error();
    const auto application_length = utf8_prefix_length(
        application_name, sizeof(SenderSharedState::application_name) - 1);
    static_assert(sizeof(DirectoryEntry::sender_name) == sizeof(SenderSharedState::name), "name capacity mismatch");
    static_assert(sizeof(DirectoryEntry::application_name) == sizeof(SenderSharedState::application_name), "application capacity mismatch");

    Registration reg{};

    generate_uuid(reg.uuid);
    std::snprintf(reg.shm_name, sizeof(reg.shm_name), "%s%.8s", kSenderShmPrefix, reg.uuid);

    auto dir_result = open_directory_locked();
    if (!dir_result.ok()) {
        return dir_result.error();
    }
    auto &dir = dir_result.value();

    auto *header = static_cast<DirectoryHeader *>(dir.mapped);
    auto *entries = reinterpret_cast<DirectoryEntry *>(
        static_cast<uint8_t *>(dir.mapped) + sizeof(DirectoryHeader));

    cleanup_stale_locked(dir);

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
    state->version = kSharedMemVersion;
    state->lifecycle_state = sender_state_open;
    state->owner_pid = owner_pid;
    state->owner_instance = owner_instance.value();
    std::memcpy(state->name, name, sender_name.size());
    std::memcpy(state->application_name, app_name, application_length);
    std::memcpy(state->uuid, reg.uuid, sizeof(state->uuid));
    state->backend = backend;
    state->width = width;
    state->height = height;
    state->format = format;
    state->ring_size = ring_size < kMinimumRingSize ? kMinimumRingSize : (ring_size > kMaxRingSlots ? kMaxRingSlots : ring_size);
    serialize_metadata(initial_metadata, state->metadata, sizeof(state->metadata));
    ipc::atomic_fence_release();
    ipc::atomic_store_release_32(&state->magic, kSenderMagic);

    // Publish the directory entry only after the complete state is readable.
    std::memset(&entry, 0, sizeof(entry));
    std::memcpy(entry.sender_name, name, sender_name.size());
    std::memcpy(entry.application_name, app_name, application_length);
    std::memcpy(entry.uuid, reg.uuid, sizeof(entry.uuid));
    std::memcpy(entry.shm_name, reg.shm_name, sizeof(entry.shm_name));
    entry.backend = backend;
    entry.pid = owner_pid;
    ipc::atomic_fence_release();
    entry.valid = entry_valid_flag::valid;
    ipc::atomic_store_relaxed(&header->change_counter,
        ipc::atomic_load_relaxed(&header->change_counter) + 1);

    ipc::shm_unmap(sender_map.value(), sizeof(SenderSharedState));
    ipc::shm_close(sender_shm.value());

    return reg;
}

Result<void> unregister_sender(const char *uuid) {
    std::lock_guard<std::mutex> lock(process_mutex_);

    auto dir_result = open_directory_locked();
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
