#pragma once

#include <nozzle/types.hpp>

namespace nozzle {

cpu_layout_desc resolve_cpu_layout(texture_format fmt);
sampling_desc resolve_sampling(texture_format fmt);
channel_order resolve_channel_order(texture_format fmt);
component_type resolve_component_type(texture_format fmt);
uint8_t resolve_bytes_per_pixel(texture_format fmt);
uint8_t resolve_channel_count(texture_format fmt);
uint8_t resolve_component_bits(texture_format fmt);
bool is_storage_compatible(texture_format a, texture_format b);
texture_format get_storage_compatible_fallback(texture_format fmt);

} // namespace nozzle
