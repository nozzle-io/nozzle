#pragma once

#include <nozzle/result.hpp>
#include <nozzle/types.hpp>

namespace nozzle {

// -- existing resolve functions --

cpu_layout_desc resolve_cpu_layout(texture_format fmt);
sampling_desc resolve_sampling(texture_format fmt);
channel_order resolve_channel_order(texture_format fmt);
component_type resolve_component_type(texture_format fmt);
uint8_t resolve_bytes_per_pixel(texture_format fmt);
uint8_t resolve_channel_count(texture_format fmt);
uint8_t resolve_component_bits(texture_format fmt);
bool is_storage_compatible(texture_format a, texture_format b);
texture_format get_storage_compatible_fallback(texture_format fmt);
texture_format get_channel_expansion_fallback(texture_format fmt);

// -- fallback policy (#31) --

enum class fallback_category : uint8_t {
    none = 0,
    storage_compatible,
    channel_expansion,
    quality_loss,
};

struct fallback_result {
    texture_format target{texture_format::unknown};
    fallback_category category{fallback_category::none};
    bool valid{false};
};

fallback_result resolve_fallback(texture_format requested, uint32_t fallback_flags);
const char *fallback_category_name(fallback_category cat) noexcept;

bool is_allowed_storage_fallback(texture_format a, texture_format b);

enum class reuse_match : uint8_t {
    exact,
    storage_compatible,
    disallowed,
};

reuse_match classify_reuse(texture_format existing, texture_format requested, uint32_t fallback_flags);

Result<channel_swizzle> derive_swizzle(texture_format semantic, texture_format storage);

Result<fallback_category> classify_observed_format(
    texture_format requested,
    texture_format observed,
    fallback_category attempted_category,
    texture_format fallback_target,
    uint32_t fallback_flags);

struct fallback_metadata {
    texture_format semantic_format{texture_format::unknown};
    texture_format storage_format{texture_format::unknown};
    channel_swizzle swizzle{channel_swizzle::identity};
};

Result<fallback_metadata> resolve_fallback_metadata(
    texture_format requested,
    texture_format observed,
    fallback_category category,
    texture_format fallback_target);

Result<void> validate_fallback_flags(uint32_t flags);

} // namespace nozzle
