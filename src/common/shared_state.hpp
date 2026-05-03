#pragma once

#include <cstdint>

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
constexpr uint64_t kSharedMemVersion = 2;

constexpr resource_id64 kInvalidSharedResourceId = 0;
constexpr process_id64 kInvalidPid = 0;
constexpr frame_number64 kInitialFrameNumber = 0;
constexpr ring_size32 kMinimumRingSize = 1;
constexpr int32_t kSlotNotFound = -1;

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

struct SenderSharedState {
    uint32_t magic{0};
    uint32_t version{0};
    char name[64]{};
    char application_name[64]{};
    char uuid[37]{};
    uint8_t backend{0};
    uint8_t channel_swizzle{0};
    uint8_t _pad0{0};
    uint32_t width{0};
    uint32_t height{0};
    uint32_t format{0};
    uint32_t semantic_format{0};
    ring_size32 ring_size{0};

    alignas(64) frame_number64 committed_frame{0};
    alignas(64) uint32_t committed_slot{0};

    struct SlotInfo {
        frame_number64 frame_number{0};
        resource_id64 shared_resource_id{0}; // IOSurface ID (Metal), HANDLE (D3D11), or slot index (DMA-BUF)
    } slots[kMaxRingSlots]{};

    char metadata[512]{};
};

} // namespace detail
} // namespace nozzle
