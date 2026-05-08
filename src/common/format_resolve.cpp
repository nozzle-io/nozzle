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
    case texture_format::rgb8_unorm:
        desc.order = channel_order::rgb;
        desc.component = component_type::unorm;
        desc.component_bits = 8;
        desc.channel_count = 3;
        desc.bytes_per_pixel = 3;
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
    case texture_format::rgb16_unorm:
        desc.order = channel_order::rgb;
        desc.component = component_type::unorm;
        desc.component_bits = 16;
        desc.channel_count = 3;
        desc.bytes_per_pixel = 6;
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
    case texture_format::rgb16_float:
        desc.order = channel_order::rgb;
        desc.component = component_type::floating;
        desc.component_bits = 16;
        desc.channel_count = 3;
        desc.bytes_per_pixel = 6;
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
    case texture_format::rgb32_float:
        desc.order = channel_order::rgb;
        desc.component = component_type::floating;
        desc.component_bits = 32;
        desc.channel_count = 3;
        desc.bytes_per_pixel = 12;
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
    case texture_format::rgb32_uint:
        desc.order = channel_order::rgb;
        desc.component = component_type::uint;
        desc.component_bits = 32;
        desc.channel_count = 3;
        desc.bytes_per_pixel = 12;
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
    case texture_format::rgb8_unorm:
    case texture_format::rgba8_unorm:
    case texture_format::bgra8_unorm:
    case texture_format::r16_unorm:
    case texture_format::rg16_unorm:
    case texture_format::rgb16_unorm:
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
    case texture_format::rgb16_float:
    case texture_format::rgba16_float:
    case texture_format::r32_float:
    case texture_format::rg32_float:
    case texture_format::rgb32_float:
    case texture_format::rgba32_float:
        desc.floating_point = true;
        break;
    case texture_format::r32_uint:
    case texture_format::rgba32_uint:
    case texture_format::rgb32_uint:
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

texture_format get_channel_expansion_fallback(texture_format fmt) {
    switch (fmt) {
    case texture_format::rgb8_unorm:
        return texture_format::rgba8_unorm;
    case texture_format::rgb16_unorm:
        return texture_format::rgba16_unorm;
    case texture_format::rgb16_float:
        return texture_format::rgba16_float;
    case texture_format::rgb32_float:
        return texture_format::rgba32_float;
    case texture_format::rgb32_uint:
        return texture_format::rgba32_uint;
    default:
        return texture_format::unknown;
    }
}

// -- fallback policy (#31) --

static texture_format get_quality_loss_fallback(texture_format fmt) {
    switch (fmt) {
    case texture_format::r32_float:     return texture_format::r16_float;
    case texture_format::rg32_float:    return texture_format::rg16_float;
    case texture_format::rgba32_float:  return texture_format::rgba16_float;
    case texture_format::r16_unorm:     return texture_format::r8_unorm;
    case texture_format::rg16_unorm:    return texture_format::rg8_unorm;
    case texture_format::rgba16_unorm:  return texture_format::rgba8_unorm;
    case texture_format::r16_float:     return texture_format::r8_unorm;
    case texture_format::rg16_float:    return texture_format::rg8_unorm;
    case texture_format::rgba16_float:  return texture_format::rgba8_unorm;
    default:                            return texture_format::unknown;
    }
}

fallback_result resolve_fallback(texture_format requested, uint32_t fallback_flags) {
    if (fallback_flags & fallback_allow_storage_compatible) {
        auto fb = get_storage_compatible_fallback(requested);
        if (fb != texture_format::unknown) {
            return {fb, fallback_category::storage_compatible, true};
        }
    }

    if (fallback_flags & fallback_allow_channel_expansion) {
        auto fb = get_channel_expansion_fallback(requested);
        if (fb != texture_format::unknown) {
            return {fb, fallback_category::channel_expansion, true};
        }
    }

    if (fallback_flags & fallback_allow_quality_loss) {
        auto fb = get_quality_loss_fallback(requested);
        if (fb != texture_format::unknown) {
            return {fb, fallback_category::quality_loss, true};
        }
    }

    return {texture_format::unknown, fallback_category::none, false};
}

const char *fallback_category_name(fallback_category cat) noexcept {
    switch (cat) {
    case fallback_category::none:               return "none";
    case fallback_category::storage_compatible: return "storage-compatible";
    case fallback_category::channel_expansion:  return "channel-expansion";
    case fallback_category::quality_loss:       return "quality-loss";
    }
    return "unknown";
}

bool is_allowed_storage_fallback(texture_format a, texture_format b) {
    return get_storage_compatible_fallback(a) == b
        || get_storage_compatible_fallback(b) == a;
}

reuse_match classify_reuse(texture_format existing, texture_format requested, uint32_t fallback_flags) {
    if (existing == requested) {
        return reuse_match::exact;
    }
    if ((fallback_flags & fallback_allow_storage_compatible) != 0
        && is_allowed_storage_fallback(existing, requested)) {
        return reuse_match::storage_compatible;
    }
    return reuse_match::disallowed;
}

Result<channel_swizzle> derive_swizzle(texture_format semantic, texture_format storage) {
    if (semantic == storage) {
        return channel_swizzle::identity;
    }
    if (is_allowed_storage_fallback(semantic, storage)) {
        return channel_swizzle::swap_rb;
    }
    return Error{ErrorCode::UnsupportedFormat,
        "unhandled format pair for swizzle derivation"};
}

Result<fallback_category> classify_observed_format(
    texture_format requested,
    texture_format observed,
    fallback_category attempted_category,
    texture_format fallback_target,
    uint32_t fallback_flags)
{
    if (observed == requested) {
        return fallback_category::none;
    }

    if (attempted_category != fallback_category::none && fallback_target != texture_format::unknown) {
        if (observed == fallback_target) {
            return attempted_category;
        }
        if ((fallback_flags & fallback_allow_storage_compatible) != 0
            && is_allowed_storage_fallback(fallback_target, observed)) {
            return attempted_category;
        }
    }

    if ((fallback_flags & fallback_allow_storage_compatible) != 0
        && is_allowed_storage_fallback(requested, observed)) {
        return fallback_category::storage_compatible;
    }

    return Error{ErrorCode::BackendError,
        "observed format does not match requested or any permitted fallback"};
}

} // namespace nozzle
