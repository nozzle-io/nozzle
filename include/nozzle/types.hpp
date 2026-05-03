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
    rgba8_srgb,   // v0.1: unsupported on Metal/IOSurface (IOSurface FourCC cannot encode sRGB semantics)
    bgra8_srgb,   // v0.1: unsupported on Metal/IOSurface (IOSurface FourCC cannot encode sRGB semantics)
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

enum class channel_swizzle : uint8_t {
    identity = 0,
    swap_rb = 1,
};

enum class texture_origin : uint8_t {
    top_left,      // Metal, CoreGraphics, Jitter matrix, IOSurface canonical
    bottom_left,   // OpenGL, TD CPUMem
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

enum class channel_order : uint8_t {
    unknown, r, rg, rgb, rgba, bgra, argb
};

enum class component_type : uint8_t {
    unknown, unorm, snorm, uint, sint, floating, depth
};

enum class format_source : uint8_t {
    requested,
    caller_hint,
    native_observed,
    registry_metadata
};

enum class native_format_kind : uint8_t {
    unknown, mtl_pixel_format, dxgi_format, drm_fourcc, gl_internal_format
};

struct native_format_desc {
    backend_type backend{backend_type::unknown};
    native_format_kind kind{native_format_kind::unknown};
    uint32_t value{0};
    uint64_t modifier{0};
};

struct cpu_layout_desc {
    channel_order order{channel_order::unknown};
    component_type component{component_type::unknown};
    uint8_t component_bits{0};
    uint8_t channel_count{0};
    uint8_t bytes_per_pixel{0};
};

struct sampling_desc {
    bool srgb{false};
    bool normalized{false};
    bool integer{false};
    bool floating_point{false};
    bool depth{false};
};

struct texture_resource_desc {
    uint32_t width{0};
    uint32_t height{0};
    uint16_t mip_levels{1};
    uint16_t array_size{1};
    uint8_t sample_count{1};
    texture_usage usage{texture_usage::none};
};

struct resolved_texture_format {
    texture_format semantic_format{texture_format::unknown};
    texture_format storage_format{texture_format::unknown};
    native_format_desc native{};
    cpu_layout_desc cpu_layout{};
    sampling_desc sampling{};
    format_source source{format_source::requested};
    channel_swizzle swizzle{channel_swizzle::identity};
};

struct frame_info {
    uint64_t frame_index{0};
    uint64_t timestamp_ns{0};
    uint32_t width{0};
    uint32_t height{0};
    texture_format format{texture_format::unknown};
    texture_format semantic_format{texture_format::unknown};
    transfer_mode transfer_mode_val{transfer_mode::unknown};
    sync_mode sync_mode_val{sync_mode::none};
    uint32_t dropped_frame_count{0};
};

struct texture_desc {
    uint32_t width{0};
    uint32_t height{0};
    texture_format format{texture_format::unknown};
    texture_format semantic_format{texture_format::unknown};
    channel_swizzle swizzle{channel_swizzle::identity};
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
    texture_format semantic_format{texture_format::unknown};
    uint32_t native_format_value{0};
    uint8_t native_format_kind{0};
    uint8_t format_source_{0};
    uint64_t native_format_modifier{0};
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
