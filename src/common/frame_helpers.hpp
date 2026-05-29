#pragma once

#include <nozzle/frame.hpp>
#include <nozzle/types.hpp>

#include <cstdint>
#include <memory>

namespace nozzle {
namespace detail {

frame make_frame(texture tex, frame_info info);
frame make_frame(texture tex, frame_info info, uint32_t slot_index);
writable_frame make_writable_frame(texture tex, texture_desc desc, uint32_t slot_index);
uint32_t get_writable_frame_slot(const writable_frame &f);
const void *get_writable_frame_state_token(const writable_frame &f);
bool writable_frame_cpu_mapping_active(const writable_frame &f);
bool writable_frame_cpu_unlock_failed(const writable_frame &f);
void mark_writable_frame_cpu_mapping_active(writable_frame &f);
void mark_writable_frame_cpu_mapping_unlocked(writable_frame &f);
void mark_writable_frame_cpu_unlock_failed(writable_frame &f);

} // namespace detail

struct frame::Impl {
    texture tex_{};
    frame_info info_{};
    uint32_t slot_index_{0};
    bool valid_{true};
};

struct writable_frame::Impl {
    texture tex_{};
    texture_desc desc_{};
    uint32_t slot_index_{0};
    bool valid_{true};
    bool cpu_mapping_active_{false};
    bool cpu_unlock_failed_{false};
};

} // namespace nozzle
