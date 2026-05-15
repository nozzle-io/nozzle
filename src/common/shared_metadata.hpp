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

inline format_fallback_info read_fallback_from_slot(const SenderSharedState::SlotInfo &slot) {
	format_fallback_info fb;
	fb.storage_format = static_cast<texture_format>(slot.format);
	fb.requested_format = static_cast<texture_format>(slot.semantic_format);
	fb.swizzle = static_cast<channel_swizzle>(slot.channel_swizzle);
	fb.fallback_target = static_cast<texture_format>(slot.fallback_target);
	fb.category = static_cast<fallback_category>(slot.fallback_category);
	fb.quality_loss = (slot.fallback_quality_loss != 0);
	return fb;
}

inline format_fallback_info read_fallback_from_global(const SenderSharedState &state) {
	format_fallback_info fb;
	fb.storage_format = static_cast<texture_format>(state.format);
	fb.requested_format = static_cast<texture_format>(state.semantic_format);
	fb.swizzle = static_cast<channel_swizzle>(state.channel_swizzle);
	fb.fallback_target = static_cast<texture_format>(state.fallback_target);
	fb.category = static_cast<fallback_category>(state.fallback_category);
	fb.quality_loss = (state.fallback_quality_loss != 0);
	return fb;
}

inline frame_info construct_frame_info_from_slot(
	const SenderSharedState::SlotInfo &si,
	uint64_t frame_index,
	uint32_t dropped) {
	frame_info info{};
	info.frame_index = frame_index;
	info.width = si.width;
	info.height = si.height;
	info.format = static_cast<texture_format>(si.format);
	info.semantic_format = static_cast<texture_format>(si.semantic_format);
	info.transfer_mode_val = transfer_mode::zero_copy_shared_texture;
	info.sync_mode_val = sync_mode::none;
	info.dropped_frame_count = dropped;
	info.fallback = read_fallback_from_slot(si);
	return info;
}

inline void update_connected_info_from_slot(
	connected_sender_info &info,
	const SenderSharedState::SlotInfo &si) {
	info.width = si.width;
	info.height = si.height;
	info.format = static_cast<texture_format>(si.format);
	info.semantic_format = static_cast<texture_format>(si.semantic_format);
	info.native_format_value = si.native_format_value;
	info.native_format_kind = si.native_format_kind;
	info.format_source_ = si.format_source;
	info.native_format_modifier = si.native_format_modifier;
	info.plane_count = si.plane_count;
	for (uint32_t i = 0; i < si.plane_count && i < 4; ++i) {
		info.plane_strides[i] = si.plane_strides[i];
		info.plane_offsets[i] = si.plane_offsets[i];
	}
	info.fallback = read_fallback_from_slot(si);
}

inline void update_connected_info_from_global(
	connected_sender_info &info,
	const SenderSharedState &state) {
	info.format = static_cast<texture_format>(state.format);
	info.semantic_format = static_cast<texture_format>(state.semantic_format);
	info.fallback = read_fallback_from_global(state);
}

} // namespace detail
} // namespace nozzle
