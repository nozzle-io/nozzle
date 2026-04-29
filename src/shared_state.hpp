#pragma once

#include <cstdint>

namespace nozzle {
namespace detail {

constexpr uint32_t kDirectoryMagic = 0x4E5A4431;
constexpr uint32_t kSenderMagic = 0x4E5A5331;
constexpr uint32_t kMaxSenders = 64;
constexpr uint32_t kMaxRingSlots = 8;
constexpr uint64_t kSharedMemVersion = 1;

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
    uint32_t valid{0};
    uint8_t _pad[4]{};
    uint64_t pid{0};
};

static_assert(sizeof(DirectoryEntry) == 280, "unexpected DirectoryEntry size");

struct SenderSharedState {
    uint32_t magic{0};
    uint32_t version{0};
    char name[64]{};
    char application_name[64]{};
    char uuid[37]{};
    uint8_t backend{0};
    uint32_t width{0};
    uint32_t height{0};
    uint32_t format{0};
    uint32_t ring_size{0};

    alignas(64) uint64_t committed_frame{0};
    alignas(64) uint32_t committed_slot{0};

    struct SlotInfo {
        uint64_t frame_number{0};
        uint64_t shared_resource_id{0}; // IOSurface ID (Metal) or HANDLE (D3D11)
    } slots[kMaxRingSlots]{};

    char metadata[512]{};
};

} // namespace detail
} // namespace nozzle
