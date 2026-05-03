#include <nozzle/format_resolve.hpp>

namespace nozzle {

cpu_layout_desc resolve_cpu_layout(texture_format fmt) {
    cpu_layout_desc desc;
    switch (fmt) {
    case texture_format::r8_unorm:
        desc.order = channel_order::r;
        desc.component = component_type::unorm;
        desc.component_bits = 8;
        desc.channel_count = 1;
        desc.bytes_per_pixel = 1;
        break;
    case texture_format::rg8_unorm:
        desc.order = channel_order::rg;
        desc.component = component_type::unorm;
        desc.component_bits = 8;
        desc.channel_count = 2;
        desc.bytes_per_pixel = 2;
        break;
    case texture_format::rgba8_unorm:
        desc.order = channel_order::rgba;
        desc.component = component_type::unorm;
        desc.component_bits = 8;
        desc.channel_count = 4;
        desc.bytes_per_pixel = 4;
        break;
    case texture_format::bgra8_unorm:
        desc.order = channel_order::bgra;
        desc.component = component_type::unorm;
        desc.component_bits = 8;
        desc.channel_count = 4;
        desc.bytes_per_pixel = 4;
        break;
    case texture_format::rgba8_srgb:
        desc.order = channel_order::rgba;
        desc.component = component_type::unorm;
        desc.component_bits = 8;
        desc.channel_count = 4;
        desc.bytes_per_pixel = 4;
        break;
    case texture_format::bgra8_srgb:
        desc.order = channel_order::bgra;
        desc.component = component_type::unorm;
        desc.component_bits = 8;
        desc.channel_count = 4;
        desc.bytes_per_pixel = 4;
        break;
    case texture_format::r16_unorm:
        desc.order = channel_order::r;
        desc.component = component_type::unorm;
        desc.component_bits = 16;
        desc.channel_count = 1;
        desc.bytes_per_pixel = 2;
        break;
    case texture_format::rg16_unorm:
        desc.order = channel_order::rg;
        desc.component = component_type::unorm;
        desc.component_bits = 16;
        desc.channel_count = 2;
        desc.bytes_per_pixel = 4;
        break;
    case texture_format::rgba16_unorm:
        desc.order = channel_order::rgba;
        desc.component = component_type::unorm;
        desc.component_bits = 16;
        desc.channel_count = 4;
        desc.bytes_per_pixel = 8;
        break;
    case texture_format::r16_float:
        desc.order = channel_order::r;
        desc.component = component_type::floating;
        desc.component_bits = 16;
        desc.channel_count = 1;
        desc.bytes_per_pixel = 2;
        break;
    case texture_format::rg16_float:
        desc.order = channel_order::rg;
        desc.component = component_type::floating;
        desc.component_bits = 16;
        desc.channel_count = 2;
        desc.bytes_per_pixel = 4;
        break;
    case texture_format::rgba16_float:
        desc.order = channel_order::rgba;
        desc.component = component_type::floating;
        desc.component_bits = 16;
        desc.channel_count = 4;
        desc.bytes_per_pixel = 8;
        break;
    case texture_format::r32_float:
        desc.order = channel_order::r;
        desc.component = component_type::floating;
        desc.component_bits = 32;
        desc.channel_count = 1;
        desc.bytes_per_pixel = 4;
        break;
    case texture_format::rg32_float:
        desc.order = channel_order::rg;
        desc.component = component_type::floating;
        desc.component_bits = 32;
        desc.channel_count = 2;
        desc.bytes_per_pixel = 8;
        break;
    case texture_format::rgba32_float:
        desc.order = channel_order::rgba;
        desc.component = component_type::floating;
        desc.component_bits = 32;
        desc.channel_count = 4;
        desc.bytes_per_pixel = 16;
        break;
    case texture_format::r32_uint:
        desc.order = channel_order::r;
        desc.component = component_type::uint;
        desc.component_bits = 32;
        desc.channel_count = 1;
        desc.bytes_per_pixel = 4;
        break;
    case texture_format::rgba32_uint:
        desc.order = channel_order::rgba;
        desc.component = component_type::uint;
        desc.component_bits = 32;
        desc.channel_count = 4;
        desc.bytes_per_pixel = 16;
        break;
    case texture_format::depth32_float:
        desc.order = channel_order::r;
        desc.component = component_type::depth;
        desc.component_bits = 32;
        desc.channel_count = 1;
        desc.bytes_per_pixel = 4;
        break;
    default:
        break;
    }
    return desc;
}

sampling_desc resolve_sampling(texture_format fmt) {
    sampling_desc desc;
    switch (fmt) {
    case texture_format::r8_unorm:
    case texture_format::rg8_unorm:
    case texture_format::rgba8_unorm:
    case texture_format::bgra8_unorm:
    case texture_format::r16_unorm:
    case texture_format::rg16_unorm:
    case texture_format::rgba16_unorm:
        desc.normalized = true;
        break;
    case texture_format::rgba8_srgb:
    case texture_format::bgra8_srgb:
        desc.srgb = true;
        desc.normalized = true;
        break;
    case texture_format::r16_float:
    case texture_format::rg16_float:
    case texture_format::rgba16_float:
    case texture_format::r32_float:
    case texture_format::rg32_float:
    case texture_format::rgba32_float:
        desc.floating_point = true;
        break;
    case texture_format::r32_uint:
    case texture_format::rgba32_uint:
        desc.integer = true;
        break;
    case texture_format::depth32_float:
        desc.depth = true;
        break;
    default:
        break;
    }
    return desc;
}

channel_order resolve_channel_order(texture_format fmt) {
    return resolve_cpu_layout(fmt).order;
}

component_type resolve_component_type(texture_format fmt) {
    return resolve_cpu_layout(fmt).component;
}

uint8_t resolve_bytes_per_pixel(texture_format fmt) {
    return resolve_cpu_layout(fmt).bytes_per_pixel;
}

uint8_t resolve_channel_count(texture_format fmt) {
    return resolve_cpu_layout(fmt).channel_count;
}

uint8_t resolve_component_bits(texture_format fmt) {
    return resolve_cpu_layout(fmt).component_bits;
}

bool is_storage_compatible(texture_format a, texture_format b) {
    if (a == texture_format::unknown || b == texture_format::unknown) return false;
    if (a == b) return true;

    auto la = resolve_cpu_layout(a);
    auto lb = resolve_cpu_layout(b);

    if (la.channel_count != lb.channel_count) return false;
    if (la.component_bits != lb.component_bits) return false;
    if (la.component != lb.component) return false;
    if (la.bytes_per_pixel != lb.bytes_per_pixel) return false;

    return true;
}

texture_format get_storage_compatible_fallback(texture_format fmt) {
    switch (fmt) {
    case texture_format::rgba8_unorm:
        return texture_format::bgra8_unorm;
    case texture_format::bgra8_unorm:
        return texture_format::rgba8_unorm;
    default:
        return texture_format::unknown;
    }
}

} // namespace nozzle
