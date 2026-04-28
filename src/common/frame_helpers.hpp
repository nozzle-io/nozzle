#pragma once

#include <bbb/nozzle/frame.hpp>
#include <bbb/nozzle/types.hpp>

#include <cstdint>
#include <memory>

namespace bbb {
namespace nozzle {
namespace detail {

frame make_frame(texture tex, frame_info info);
writable_frame make_writable_frame(texture tex, texture_desc desc, uint32_t slot_index);
uint32_t get_writable_frame_slot(const writable_frame &f);

} // namespace detail

struct frame::Impl {
    texture tex_{};
    frame_info info_{};
    bool valid_{true};
};

struct writable_frame::Impl {
    texture tex_{};
    texture_desc desc_{};
    uint32_t slot_index_{0};
    bool valid_{true};
};

} // namespace nozzle
} // namespace bbb
