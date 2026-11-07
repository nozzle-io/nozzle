#pragma once

#include "shared_state.hpp"
#include "ipc.hpp"
#include <nozzle/result.hpp>

#include <cstddef>
#include <cstring>
#include <string>

namespace nozzle {
namespace detail {

inline Result<void> validate_shared_prefix(uint32_t magic, uint32_t version,
                                           uint32_t expected_magic, const char *kind) {
    if (magic != expected_magic) {
        return Error{ErrorCode::BackendError, std::string(kind) + " has invalid magic"};
    }
    if (version != kSharedMemVersion) {
        return Error{ErrorCode::BackendError,
            std::string(kind) + " shared memory version mismatch: expected " +
            std::to_string(kSharedMemVersion) + ", got " + std::to_string(version)};
    }
    return {};
}

// Read the stable prefix before mapping a version-dependent full structure so
// older, smaller layouts still produce a useful version diagnostic.
inline Result<void> validate_mapping_prefix(ipc::shm_handle &handle,
                                            uint32_t expected_magic, const char *kind) {
    static_assert(offsetof(DirectoryHeader, version) == sizeof(uint32_t), "directory prefix changed");
    static_assert(offsetof(SenderSharedState, version) == sizeof(uint32_t), "sender prefix changed");
    uint32_t prefix[2]{};
    auto mapped = ipc::shm_map(handle, sizeof(prefix), true);
    if (!mapped.ok()) return mapped.error();
    std::memcpy(prefix, mapped.value(), sizeof(prefix));
    ipc::shm_unmap(mapped.value(), sizeof(prefix));
    return validate_shared_prefix(prefix[0], prefix[1], expected_magic, kind);
}

template <std::size_t size>
inline bool terminated_field(const char (&value)[size]) noexcept {
    return std::memchr(value, '\0', size) != nullptr;
}

inline Result<uint32_t> validate_directory_header(const DirectoryHeader &header) {
    const auto magic = header.magic;
    const auto version = header.version;
    const auto capacity = header.capacity;
    auto prefix = validate_shared_prefix(magic, version, kDirectoryMagic, "directory");
    if (!prefix.ok()) return prefix.error();
    if (capacity == 0 || capacity > kMaxSenders) {
        return Error{ErrorCode::BackendError, "nozzle directory has invalid capacity"};
    }
    return capacity;
}

inline bool valid_directory_strings(const DirectoryEntry &entry) noexcept {
    return terminated_field(entry.sender_name) &&
           terminated_field(entry.application_name) &&
           terminated_field(entry.uuid) && terminated_field(entry.shm_name);
}

inline Result<uint32_t> validate_sender_layout(const SenderSharedState &state) {
    const auto magic = state.magic;
    const auto version = state.version;
    const auto ring_size = state.ring_size;
    auto prefix = validate_shared_prefix(magic, version, kSenderMagic, "sender");
    if (!prefix.ok()) return prefix.error();
    if (ring_size < kMinimumRingSize || ring_size > kMaxRingSlots) {
        return Error{ErrorCode::BackendError, "sender state has invalid ring size"};
    }
    if (!terminated_field(state.name) || !terminated_field(state.application_name) ||
        !terminated_field(state.uuid)) {
        return Error{ErrorCode::BackendError, "sender state has unterminated identity fields"};
    }
    return ring_size;
}

} // namespace detail
} // namespace nozzle
