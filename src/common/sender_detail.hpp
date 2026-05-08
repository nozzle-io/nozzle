#pragma once

#include <cstdint>

#include <nozzle/channel_swizzle.hpp>
#include <nozzle/format_resolve.hpp>
#include <nozzle/texture.hpp>

#include "shared_state.hpp"

namespace nozzle {
namespace detail {

inline void write_slot_metadata(SenderSharedState::SlotInfo &slot,
                                uint64_t frame_number,
                                resource_id64 resource_id,
                                uint32_t width,
                                uint32_t height,
                                texture_format storage_format,
                                texture_format semantic_format,
                                channel_swizzle swizzle,
                                const resolved_texture_format &resolved) {
	slot.frame_number = frame_number;
	slot.shared_resource_id = resource_id;
	slot.width = width;
	slot.height = height;
	slot.format = static_cast<uint32_t>(storage_format);
	slot.semantic_format = static_cast<uint32_t>(semantic_format);
	slot.channel_swizzle = static_cast<uint8_t>(swizzle);
	slot.native_format_kind = static_cast<uint8_t>(resolved.native.kind);
	slot.format_source = static_cast<uint8_t>(resolved.source);
	slot.native_format_value = resolved.native.value;
	slot.native_format_modifier = resolved.native.modifier;
	slot.plane_count = resolved.native.plane_count;
	for (uint32_t i = 0; i < resolved.native.plane_count && i < 4; ++i) {
		slot.plane_strides[i] = resolved.native.plane_strides[i];
		slot.plane_offsets[i] = resolved.native.plane_offsets[i];
	}
}

inline void write_global_metadata(SenderSharedState &state,
                                  uint32_t width,
                                  uint32_t height,
                                  texture_format storage_format,
                                  texture_format semantic_format,
                                  channel_swizzle swizzle) {
	state.width = width;
	state.height = height;
	state.format = static_cast<uint32_t>(storage_format);
	state.semantic_format = static_cast<uint32_t>(semantic_format);
	state.channel_swizzle = static_cast<uint8_t>(swizzle);
}

} // namespace detail
} // namespace nozzle
