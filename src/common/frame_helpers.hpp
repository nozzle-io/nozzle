#pragma once

#include <nozzle/frame.hpp>
#include <nozzle/pixel_access.hpp>
#include <nozzle/result.hpp>
#include <nozzle/types.hpp>

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>

namespace nozzle {
namespace detail {

struct writable_cpu_mapping_state {
    std::atomic<uint32_t> reference_count{1};
    std::mutex mutex;
    bool active{false};
    bool unlock_failed{false};
    uint64_t generation{0};
};

class writable_cpu_mapping_state_ref {
public:
    writable_cpu_mapping_state_ref() noexcept = default;
    explicit writable_cpu_mapping_state_ref(writable_cpu_mapping_state *state) noexcept;
    writable_cpu_mapping_state_ref(const writable_cpu_mapping_state_ref &other) noexcept;
    writable_cpu_mapping_state_ref &operator=(const writable_cpu_mapping_state_ref &other) noexcept;
    writable_cpu_mapping_state_ref(writable_cpu_mapping_state_ref &&other) noexcept;
    writable_cpu_mapping_state_ref &operator=(writable_cpu_mapping_state_ref &&other) noexcept;
    ~writable_cpu_mapping_state_ref() noexcept;

    writable_cpu_mapping_state *get() const noexcept;
    explicit operator bool() const noexcept;
    void reset() noexcept;

private:
    writable_cpu_mapping_state *state_{nullptr};
};

frame make_frame(texture tex, frame_info info);
frame make_frame(texture tex, frame_info info, uint32_t slot_index);
Result<writable_frame> make_writable_frame(texture tex, texture_desc desc, uint32_t slot_index);
uint32_t get_writable_frame_slot(const writable_frame &f);
const void *get_writable_frame_state_token(const writable_frame &f);
bool writable_frame_cpu_mapping_active(const writable_frame &f);
bool writable_frame_cpu_unlock_failed(const writable_frame &f);
void mark_writable_frame_cpu_mapping_active(writable_frame &f);
void mark_writable_frame_cpu_mapping_unlocked(writable_frame &f);
void mark_writable_frame_cpu_unlock_failed(writable_frame &f);
Result<writable_cpu_mapping_state_ref> begin_writable_frame_cpu_mapping(writable_frame &f);
void mark_writable_cpu_mapping_unlocked(writable_cpu_mapping_state_ref &state_ref);
void mark_writable_cpu_mapping_unlock_failed(writable_cpu_mapping_state_ref &state_ref);
Result<mapped_pixels> lock_legacy_frame_pixels_with_origin(const frame &f, texture_origin desired_origin);
void unlock_legacy_frame_pixels(const frame &f);
Result<mapped_pixels> lock_legacy_writable_pixels_with_origin(writable_frame &f, texture_origin desired_origin);
Result<void> unlock_legacy_writable_pixels_checked(writable_frame &f);

} // namespace detail

struct frame::Impl {
    texture tex_{};
    frame_info info_{};
    uint32_t slot_index_{0};
    bool valid_{true};
    mutable std::mutex legacy_mutex_{};
    mutable pixel_mapping legacy_read_mapping_{};
};

struct writable_frame::Impl {
    texture tex_{};
    texture_desc desc_{};
    uint32_t slot_index_{0};
    bool valid_{true};
    detail::writable_cpu_mapping_state_ref cpu_mapping_state_{};
    mutable std::mutex legacy_mutex_{};
    pixel_mapping legacy_writable_mapping_{};
};

} // namespace nozzle
