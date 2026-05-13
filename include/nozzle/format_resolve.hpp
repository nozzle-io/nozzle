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

// Single-step fallback policy (#35):
//   1. Exact format is attempted first (caller responsibility).
//   2. resolve_fallback() returns at most ONE categorized fallback target.
//      It does NOT chain: if a fallback target itself is unsupported, the
//      caller returns an error — resolve_fallback() is never called again.
//   3. Category priority is fixed: storage_compatible > channel_expansion > quality_loss.
//      Only one category is returned per call; no category crossing.
//   4. quality_loss is opt-in only (requires fallback_allow_quality_loss flag).
//      Without the flag, no format ever downgrades to lower precision.
//      When enabled: 32F→16F, 16F→8unorm (no 8-bit float exists), unorm16→unorm8.
//   5. The caller (sender) must attempt at most one call to resolve_fallback()
//      per frame acquisition. On failure of the fallback target, return error.
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

// -- single-step attempt plan (#35) --

struct texture_attempt_plan {
    texture_format primary{texture_format::unknown};
    texture_format fallback{texture_format::unknown};
    fallback_category fallback_cat{fallback_category::none};
    bool has_fallback{false};
};

texture_attempt_plan plan_texture_create(texture_format requested, uint32_t fallback_flags);

} // namespace nozzle
