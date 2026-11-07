#pragma once

#include <cstdint>
#include <cstddef>

namespace nozzle {
namespace detail {

using resource_id64 = uint64_t;   // IOSurface ID (Metal), HANDLE value (D3D11), or slot index (DMA-BUF)
using process_id64 = uint64_t;
using frame_number64 = uint64_t;
using ring_size32 = uint32_t;

enum class entry_valid_flag : uint32_t {
    invalid = 0,
    valid = 1,
};

constexpr uint32_t kDirectoryMagic = 0x4E5A4431;
constexpr uint32_t kSenderMagic = 0x4E5A5331;
constexpr uint32_t kMaxSenders = 64;
constexpr uint32_t kMaxRingSlots = 8;
constexpr uint64_t kSharedMemVersion = 7;
// Directory bytes have been unchanged across historical protocol versions 2..6.
// Sender-only version changes must not invalidate the persistent directory.
constexpr uint32_t kDirectoryLayoutVersion = 6;
constexpr uint32_t kMinimumDirectoryLayoutVersion = 2;

constexpr resource_id64 kInvalidSharedResourceId = 0;
constexpr process_id64 kInvalidPid = 0;
constexpr frame_number64 kInitialFrameNumber = 0;
constexpr ring_size32 kMinimumRingSize = 1;
constexpr int32_t kSlotNotFound = -1;
constexpr uint32_t kMetadataSize = 512;
constexpr uint32_t sender_state_open = 0;
constexpr uint32_t sender_state_closed = 1;

constexpr const char kDirectoryShmName[] = "/nozzle_dir";
constexpr const char kSenderShmPrefix[] = "/nozzle_";

struct DirectoryHeader {
    uint32_t magic{0};
    uint32_t version{0};
    uint32_t capacity{0};
    uint32_t padding{0};
    uint64_t change_counter{0};
};

struct DirectoryEntry {
    char sender_name[64]{};
    char application_name[64]{};
    char uuid[37]{};
    char shm_name[96]{};
    uint8_t backend{0};
    entry_valid_flag valid{entry_valid_flag::invalid};
    uint8_t _pad[4]{};
    process_id64 pid{0};
};

static_assert(sizeof(DirectoryEntry) == 280, "unexpected DirectoryEntry size");
static_assert(sizeof(DirectoryHeader) == 24, "unexpected DirectoryHeader size");
static_assert(offsetof(DirectoryHeader, magic) == 0 &&
              offsetof(DirectoryHeader, version) == 4 &&
              offsetof(DirectoryHeader, capacity) == 8 &&
              offsetof(DirectoryHeader, padding) == 12 &&
              offsetof(DirectoryHeader, change_counter) == 16, "directory header layout changed");
static_assert(offsetof(DirectoryEntry, sender_name) == 0 &&
              offsetof(DirectoryEntry, application_name) == 64 &&
              offsetof(DirectoryEntry, uuid) == 128 &&
              offsetof(DirectoryEntry, shm_name) == 165 &&
              offsetof(DirectoryEntry, backend) == 261 &&
              offsetof(DirectoryEntry, valid) == 264 &&
              offsetof(DirectoryEntry, _pad) == 268 &&
              offsetof(DirectoryEntry, pid) == 272, "directory entry layout changed");

struct SenderSharedState {
    uint32_t magic{0};
    uint32_t version{0};
    char name[64]{};
    char application_name[64]{};
    char uuid[37]{};
    uint8_t backend{0};
    uint8_t channel_swizzle{0};
    uint8_t fallback_category{0};
    uint8_t fallback_quality_loss{0};
    uint32_t fallback_target{0};
    uint8_t _pad0{0};
    uint32_t width{0};
    uint32_t height{0};
    uint32_t format{0};
    uint32_t semantic_format{0};
    ring_size32 ring_size{0};
    // Uses existing v6 padding. Older senders zero the entire mapping, so zero
    // means open; they do not publish closure when destroyed in a live process.
    uint32_t lifecycle_state{sender_state_open};

    alignas(64) frame_number64 committed_frame{0};
    alignas(64) uint32_t committed_slot{0};

    struct SlotInfo {
        frame_number64 frame_number{0};
        resource_id64 shared_resource_id{0};
        uint32_t width{0};
        uint32_t height{0};
        uint32_t format{0};
        uint32_t semantic_format{0};
        uint8_t channel_swizzle{0};
        uint8_t native_format_kind{0};
        uint8_t format_source{0};
        uint8_t _pad{};
        uint32_t native_format_value{0};
        uint64_t native_format_modifier{0};
        uint32_t plane_count{0};
        uint32_t plane_strides[4]{0};
        uint32_t plane_offsets[4]{0};
        uint32_t fallback_target{0};
        uint8_t fallback_category{0};
        uint8_t fallback_quality_loss{0};
        uint8_t _pad_fb{0};
    } slots[kMaxRingSlots]{};

    char metadata[kMetadataSize]{};
};

static_assert(sizeof(SenderSharedState::SlotInfo) == 96, "unexpected SlotInfo size");
static_assert(offsetof(SenderSharedState, lifecycle_state) == 208,
              "unexpected sender lifecycle offset");
static_assert(sizeof(SenderSharedState) == 1664, "unexpected SenderSharedState size");

} // namespace detail
} // namespace nozzle
