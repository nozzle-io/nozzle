#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace nozzle {

enum class backend_type {
    unknown = 0,
    d3d11 = 1,
    metal = 2,
    opengl = 3,
    dma_buf = 4,
};

enum class texture_format {
    unknown = 0,
    r8_unorm = 1,
    rg8_unorm = 2,
    rgb8_unorm = 3,
    rgba8_unorm = 4,
    bgra8_unorm = 5,
    rgba8_srgb = 6,
    bgra8_srgb = 7,
    r16_unorm = 8,
    rg16_unorm = 9,
    rgb16_unorm = 10,
    rgba16_unorm = 11,
    r16_float = 12,
    rg16_float = 13,
    rgb16_float = 14,
    rgba16_float = 15,
    r32_float = 16,
    rg32_float = 17,
    rgb32_float = 18,
    rgba32_float = 19,
    r32_uint = 20,
    rgba32_uint = 21,
    rgb32_uint = 22,
    depth32_float = 23,
};

enum class transfer_mode {
    unknown = 0,
    zero_copy_shared_texture = 1,
    gpu_copy = 2,
    cpu_copy = 3,
};

enum class sync_mode {
    none = 0,
    access_guarded = 1,
    gpu_fence_best_effort = 2,
};

enum class receive_mode {
    latest_only = 0,
    sequential_best_effort = 1,
};

enum class frame_status {
    new_frame = 0,
    no_new_frame = 1,
    dropped_frames = 2,
    sender_closed = 3,
    error = 4,
};

enum class channel_swizzle : uint8_t {
    identity = 0,
    swap_rb = 1,
};

enum class fallback_category : uint8_t {
    none = 0,
    storage_compatible = 1,
    channel_expansion = 2,
    quality_loss = 3,
};

struct format_fallback_info {
    texture_format requested_format{texture_format::unknown};
    texture_format storage_format{texture_format::unknown};
    texture_format fallback_target{texture_format::unknown};
    fallback_category category{fallback_category::none};
    channel_swizzle swizzle{channel_swizzle::identity};
    bool quality_loss{false};
};

enum class texture_origin : uint8_t {
    top_left = 0,
    bottom_left = 1,
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
    requested = 0,
    caller_hint = 1,
    native_observed = 2,
    registry_metadata = 3
};

enum class native_format_kind : uint8_t {
    unknown = 0,
    mtl_pixel_format = 1,
    dxgi_format = 2,
    drm_fourcc = 3,
    gl_internal_format = 4
};

struct native_format_desc {
    backend_type backend{backend_type::unknown};
    native_format_kind kind{native_format_kind::unknown};
    uint32_t value{0};
    uint64_t modifier{0};
    uint32_t plane_count{0};
    uint32_t plane_strides[4]{0};
    uint32_t plane_offsets[4]{0};
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
    format_fallback_info fallback{};
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
    uint32_t plane_count{0};
    uint32_t plane_strides[4]{0};
    uint32_t plane_offsets[4]{0};
    double estimated_fps{0.0};
    uint64_t frame_counter{0};
    uint64_t last_update_time_ns{0};
    format_fallback_info fallback{};
};

struct key_value {
    std::string key{};
    std::string value{};
};

using metadata_list = std::vector<key_value>;

constexpr uint32_t fallback_none{0};
constexpr uint32_t fallback_allow_storage_compatible{1u << 0};
constexpr uint32_t fallback_allow_channel_expansion{1u << 1};
constexpr uint32_t fallback_allow_quality_loss{1u << 2};
constexpr uint32_t fallback_safe_defaults{fallback_allow_storage_compatible | fallback_allow_channel_expansion};

struct native_device_desc {
    backend_type backend{backend_type::unknown};
    void *device{nullptr};
    void *context{nullptr};
};

struct sender_desc {
    std::string name{};
    std::string application_name{};
    uint32_t ring_buffer_size{3};
    metadata_list metadata{};
    uint32_t fallback_flags{fallback_safe_defaults};
    native_device_desc native_device{};
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
