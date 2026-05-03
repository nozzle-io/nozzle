#pragma once

#include <cstdint>
#include <nozzle/result.hpp>
#include <nozzle/types.hpp>

namespace nozzle {

struct channel_permute {
	uint8_t map[4]; // source channel index for each destination channel, in memory channel order
};

// ARGB↔BGRA: full byte reversal is its own inverse — applying twice yields identity
constexpr channel_permute permute_argb_to_bgra{{3, 2, 1, 0}};
constexpr channel_permute permute_bgra_to_argb{{3, 2, 1, 0}};

// ARGB↔RGBA: channel rotation pair — inverse of each other
constexpr channel_permute permute_argb_to_rgba{{1, 2, 3, 0}};
constexpr channel_permute permute_rgba_to_argb{{3, 0, 1, 2}};

// out-of-place 4-channel pixel swizzle
// supported formats: rgba8_unorm, bgra8_unorm, rgba8_srgb, bgra8_srgb, rgba32_float
// unsupported formats return Error{UnsupportedFormat}
// format determines bytes-per-pixel (4 or 16) and vImage dispatch
// channel_permute determines the byte position mapping
Result<void> swizzle_channels(
	const void *src, void *dst,
	uint32_t width, uint32_t height,
	uint32_t src_row_bytes, uint32_t dst_row_bytes,
	texture_format format,
	channel_permute permute);

} // namespace nozzle
