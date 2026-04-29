#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace nozzle {

enum class backend_type {
    unknown,
    d3d11,
    metal,
    opengl,
    dma_buf,
};

enum class texture_format {
    unknown,
    r8_unorm,
    rg8_unorm,
    rgba8_unorm,
    bgra8_unorm,
    rgba8_srgb,
    bgra8_srgb,
    r16_unorm,
    rg16_unorm,
    rgba16_unorm,
    r16_float,
    rg16_float,
    rgba16_float,
    r32_float,
    rg32_float,
    rgba32_float,
    r32_uint,
    rgba32_uint,
    depth32_float,
};

enum class transfer_mode {
    unknown,
    zero_copy_shared_texture,
    gpu_copy,
    cpu_copy,
};

enum class sync_mode {
    none,
    access_guarded,
    gpu_fence_best_effort,
};

enum class receive_mode {
    latest_only,
    sequential_best_effort,
};

enum class frame_status {
    new_frame,
    no_new_frame,
    dropped_frames,
    sender_closed,
    error,
};

enum class texture_usage : uint32_t {
    none = 0,
    shader_read = 1 << 0,
    shader_write = 1 << 1,
    render_target = 1 << 2,
    shared = 1 << 3,
};

constexpr texture_usage operator|(texture_usage a, texture_usage b) {
    return static_cast<texture_usage>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

constexpr texture_usage operator&(texture_usage a, texture_usage b) {
    return static_cast<texture_usage>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}

struct frame_info {
    uint64_t frame_index{0};
    uint64_t timestamp_ns{0};
    uint32_t width{0};
    uint32_t height{0};
    texture_format format{texture_format::unknown};
    transfer_mode transfer_mode_val{transfer_mode::unknown};
    sync_mode sync_mode_val{sync_mode::none};
    uint32_t dropped_frame_count{0};
};

struct texture_desc {
    uint32_t width{0};
    uint32_t height{0};
    texture_format format{texture_format::unknown};
    texture_usage usage{texture_usage::shader_read | texture_usage::shared};
};

struct texture_layout {
    uint32_t row_pitch_bytes{0};
    uint32_t slice_pitch_bytes{0};
    uint32_t alignment_bytes{0};
};

struct texture_format_desc {
    texture_format common_format{texture_format::unknown};
    uint32_t native_format{0};
};

struct sender_info {
    std::string name{};
    std::string application_name{};
    std::string id{};
    backend_type backend{backend_type::unknown};
};

struct connected_sender_info {
    std::string name{};
    std::string application_name{};
    std::string id{};
    backend_type backend{backend_type::unknown};
    uint32_t width{0};
    uint32_t height{0};
    texture_format format{texture_format::unknown};
    double estimated_fps{0.0};
    uint64_t frame_counter{0};
    uint64_t last_update_time_ns{0};
};

struct key_value {
    std::string key{};
    std::string value{};
};

using metadata_list = std::vector<key_value>;

struct sender_desc {
    std::string name{};
    std::string application_name{};
    uint32_t ring_buffer_size{3};
    metadata_list metadata{};
    bool allow_format_fallback{true};
};

struct receiver_desc {
    std::string name{};
    std::string application_name{};
    receive_mode receive_mode_val{receive_mode::latest_only};
};

struct acquire_desc {
    uint64_t timeout_ms{0};
};

} // namespace nozzle
