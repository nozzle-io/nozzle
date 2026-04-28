#pragma once

#include <cstdint>
#include <unistd.h>

namespace bbb {
namespace nozzle {
namespace detail {

constexpr uint32_t kDirectoryMagic = 0x4E5A4431; // 'NZD1'
constexpr uint32_t kSenderMagic = 0x4E5A5331;    // 'NZS1'
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
    pid_t pid{0};
    uint32_t valid{0};
};

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

    alignas(64) _Atomic uint64_t committed_frame;
    alignas(64) _Atomic uint32_t committed_slot;

    struct SlotInfo {
        uint64_t frame_number{0};
        uint32_t iosurface_id{0};
        uint32_t shared_handle{0};
    } slots[kMaxRingSlots]{};

    char metadata[512]{};
};

} // namespace detail
} // namespace nozzle
} // namespace bbb
