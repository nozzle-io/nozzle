#pragma once

#include <cstdint>

#include <nozzle/format_resolve.hpp>
#include <nozzle/texture.hpp>

#include "common/shared_state.hpp"

namespace nozzle {
namespace detail {

inline void write_slot_metadata(SenderSharedState::SlotInfo &slot,
                                uint64_t frame_number,
                                resource_id64 resource_id,
                                uint32_t width,
                                uint32_t height,
                                const resolved_texture_format &resolved,
                                const format_fallback_info &fallback) {
	slot.frame_number = frame_number;
	slot.shared_resource_id = resource_id;
	slot.width = width;
	slot.height = height;
	slot.format = static_cast<uint32_t>(fallback.storage_format);
	slot.semantic_format = static_cast<uint32_t>(fallback.requested_format);
	slot.channel_swizzle = static_cast<uint8_t>(fallback.swizzle);
	slot.native_format_kind = static_cast<uint8_t>(resolved.native.kind);
	slot.format_source = static_cast<uint8_t>(resolved.source);
	slot.native_format_value = resolved.native.value;
	slot.native_format_modifier = resolved.native.modifier;
	slot.plane_count = resolved.native.plane_count;
	for (uint32_t i = 0; i < resolved.native.plane_count && i < 4; ++i) {
		slot.plane_strides[i] = resolved.native.plane_strides[i];
		slot.plane_offsets[i] = resolved.native.plane_offsets[i];
	}
	slot.fallback_target = static_cast<uint32_t>(fallback.fallback_target);
	slot.fallback_category = static_cast<uint8_t>(fallback.category);
	slot.fallback_quality_loss = fallback.quality_loss ? 1 : 0;
}

inline void write_global_metadata(SenderSharedState &state,
                                  uint32_t width,
                                  uint32_t height,
                                  const format_fallback_info &fallback) {
	state.width = width;
	state.height = height;
	state.format = static_cast<uint32_t>(fallback.storage_format);
	state.semantic_format = static_cast<uint32_t>(fallback.requested_format);
	state.channel_swizzle = static_cast<uint8_t>(fallback.swizzle);
	state.fallback_target = static_cast<uint32_t>(fallback.fallback_target);
	state.fallback_category = static_cast<uint8_t>(fallback.category);
	state.fallback_quality_loss = fallback.quality_loss ? 1 : 0;
}

} // namespace detail
} // namespace nozzle
