#include <nozzle/backend_capabilities.hpp>
#include <nozzle/format_resolve.hpp>

#include "backend_capabilities_docs.hpp"

#include <sstream>

namespace nozzle {
namespace {

constexpr uint64_t bits() noexcept {
    return 0;
}

template <typename ... formats>
constexpr uint64_t bits(texture_format first, formats ... rest) noexcept {
    return texture_format_bit(first) | bits(rest ...);
}

constexpr uint32_t flag(backend_capability_flags value) noexcept {
    return static_cast<uint32_t>(value);
}

constexpr uint32_t sharing(backend_sharing_mechanism value) noexcept {
    return static_cast<uint32_t>(value);
}

constexpr uint64_t requested_color_formats =
    bits(texture_format::r8_unorm,
        texture_format::rg8_unorm,
        texture_format::rgb8_unorm,
        texture_format::rgba8_unorm,
        texture_format::bgra8_unorm,
        texture_format::rgba8_srgb,
        texture_format::bgra8_srgb,
        texture_format::r16_unorm,
        texture_format::rg16_unorm,
        texture_format::rgb16_unorm,
        texture_format::rgba16_unorm,
        texture_format::r16_float,
        texture_format::rg16_float,
        texture_format::rgb16_float,
        texture_format::rgba16_float,
        texture_format::r32_float,
        texture_format::rg32_float,
        texture_format::rgb32_float,
        texture_format::rgba32_float,
        texture_format::r32_uint,
        texture_format::rgb32_uint,
        texture_format::rgba32_uint);

constexpr uint64_t four_channel_storage_formats =
    bits(texture_format::r8_unorm,
        texture_format::rg8_unorm,
        texture_format::rgba8_unorm,
        texture_format::bgra8_unorm,
        texture_format::rgba8_srgb,
        texture_format::bgra8_srgb,
        texture_format::r16_unorm,
        texture_format::rg16_unorm,
        texture_format::rgba16_unorm,
        texture_format::r16_float,
        texture_format::rg16_float,
        texture_format::rgba16_float,
        texture_format::r32_float,
        texture_format::rg32_float,
        texture_format::rgba32_float,
        texture_format::r32_uint,
        texture_format::rgba32_uint);

constexpr uint64_t metal_requested_formats =
    requested_color_formats &
    ~texture_format_bit(texture_format::rgba8_srgb) &
    ~texture_format_bit(texture_format::bgra8_srgb);

constexpr uint64_t metal_iosurface_storage_formats =
    bits(texture_format::r8_unorm,
        texture_format::rg8_unorm,
        texture_format::rgba8_unorm,
        texture_format::bgra8_unorm,
        texture_format::r16_unorm,
        texture_format::rg16_unorm,
        texture_format::rgba16_unorm,
        texture_format::r16_float,
        texture_format::rg16_float,
        texture_format::rgba16_float,
        texture_format::r32_float,
        texture_format::rg32_float,
        texture_format::rgba32_float,
        texture_format::r32_uint,
        texture_format::rgba32_uint);

constexpr uint64_t dmabuf_direct_formats =
    bits(texture_format::r8_unorm,
        texture_format::rg8_unorm,
        texture_format::rgb8_unorm,
        texture_format::rgba8_unorm,
        texture_format::bgra8_unorm,
        texture_format::r16_unorm,
        texture_format::rg16_unorm,
        texture_format::rgb16_unorm,
        texture_format::rgba16_unorm,
        texture_format::r32_uint,
        texture_format::rgb32_uint,
        texture_format::rgba32_uint);

constexpr uint64_t opengl_interop_formats =
    bits(texture_format::r8_unorm,
        texture_format::rg8_unorm,
        texture_format::rgb8_unorm,
        texture_format::rgba8_unorm,
        texture_format::bgra8_unorm,
        texture_format::r16_unorm,
        texture_format::rg16_unorm,
        texture_format::rgb16_unorm,
        texture_format::rgba16_unorm,
        texture_format::r16_float,
        texture_format::rg16_float,
        texture_format::rgb16_float,
        texture_format::rgba16_float,
        texture_format::r32_float,
        texture_format::rg32_float,
        texture_format::rgb32_float,
        texture_format::rgba32_float,
        texture_format::r32_uint,
        texture_format::rgb32_uint,
        texture_format::rgba32_uint);

constexpr uint64_t known_quality_loss_formats =
    bits(texture_format::r32_float,
        texture_format::rg32_float,
        texture_format::rgba32_float,
        texture_format::r16_unorm,
        texture_format::rg16_unorm,
        texture_format::rgba16_unorm,
        texture_format::r16_float,
        texture_format::rg16_float,
        texture_format::rgba16_float);

constexpr backend_capabilities metal_capabilities{
    backend_capabilities_version,
    backend_type::metal,
    flag(backend_capability_flags::sender) |
        flag(backend_capability_flags::receiver) |
        flag(backend_capability_flags::writable_frames) |
        flag(backend_capability_flags::native_texture_publish) |
        flag(backend_capability_flags::direct_external_publish) |
        flag(backend_capability_flags::cpu_read) |
        flag(backend_capability_flags::cpu_write) |
        flag(backend_capability_flags::zero_copy_receive) |
        flag(backend_capability_flags::zero_copy_publish) |
        flag(backend_capability_flags::requires_matching_backend),
    sharing(backend_sharing_mechanism::iosurface),
    native_format_kind::mtl_pixel_format,
    fallback_safe_defaults,
    0,
    metal_requested_formats,
    metal_iosurface_storage_formats,
    metal_iosurface_storage_formats,
    metal_iosurface_storage_formats,
    metal_iosurface_storage_formats,
    metal_iosurface_storage_formats,
    known_quality_loss_formats,
};

constexpr backend_capabilities d3d11_capabilities{
    backend_capabilities_version,
    backend_type::d3d11,
    flag(backend_capability_flags::sender) |
        flag(backend_capability_flags::receiver) |
        flag(backend_capability_flags::writable_frames) |
        flag(backend_capability_flags::native_texture_publish) |
        flag(backend_capability_flags::cpu_read) |
        flag(backend_capability_flags::cpu_write) |
        flag(backend_capability_flags::zero_copy_receive) |
        flag(backend_capability_flags::zero_copy_publish) |
        flag(backend_capability_flags::requires_matching_backend),
    sharing(backend_sharing_mechanism::d3d11_nt_handle),
    native_format_kind::dxgi_format,
    fallback_safe_defaults,
    0,
    requested_color_formats,
    four_channel_storage_formats,
    four_channel_storage_formats,
    0,
    four_channel_storage_formats,
    four_channel_storage_formats,
    known_quality_loss_formats,
};

constexpr backend_capabilities dma_buf_capabilities{
    backend_capabilities_version,
    backend_type::dma_buf,
    flag(backend_capability_flags::sender) |
        flag(backend_capability_flags::receiver) |
        flag(backend_capability_flags::writable_frames) |
        flag(backend_capability_flags::direct_external_publish) |
        flag(backend_capability_flags::cpu_read) |
        flag(backend_capability_flags::cpu_write) |
        flag(backend_capability_flags::zero_copy_publish) |
        flag(backend_capability_flags::single_sender_per_process) |
        flag(backend_capability_flags::may_require_runtime_probe),
    sharing(backend_sharing_mechanism::dma_buf),
    native_format_kind::drm_fourcc,
    fallback_safe_defaults,
    1,
    requested_color_formats,
    dmabuf_direct_formats,
    0,
    dmabuf_direct_formats,
    dmabuf_direct_formats,
    dmabuf_direct_formats,
    known_quality_loss_formats,
};

constexpr backend_capabilities opengl_capabilities{
    backend_capabilities_version,
    backend_type::opengl,
    flag(backend_capability_flags::native_texture_publish) |
        flag(backend_capability_flags::may_require_runtime_probe),
    sharing(backend_sharing_mechanism::opengl_texture),
    native_format_kind::gl_internal_format,
    fallback_safe_defaults,
    0,
    opengl_interop_formats,
    0,
    opengl_interop_formats,
    0,
    0,
    0,
    known_quality_loss_formats,
};

const backend_capabilities *lookup_capabilities(backend_type backend) noexcept {
    switch (backend) {
        case backend_type::metal: return &metal_capabilities;
        case backend_type::d3d11: return &d3d11_capabilities;
        case backend_type::dma_buf: return &dma_buf_capabilities;
        case backend_type::opengl: return &opengl_capabilities;
        case backend_type::unknown:
        default: return nullptr;
    }
}

void append_format_list(std::ostringstream &out, uint64_t format_bits) {
    bool first = true;
    for (uint32_t value = 1; value <= static_cast<uint32_t>(texture_format::depth32_float); ++value) {
        auto format = static_cast<texture_format>(value);
        if (!supports_format(format_bits, format)) {
            continue;
        }
        if (!first) {
            out << ", ";
        }
        out << texture_format_name(format);
        first = false;
    }
    if (first) {
        out << "none";
    }
}

void append_capability_row(std::ostringstream &out, const backend_capabilities &caps) {
    out << "| " << backend_type_name(caps.backend) << " | ";
    out << caps.capability_flags << " | ";
    out << caps.sharing_mechanisms << " | ";
    out << static_cast<uint32_t>(caps.native_kind) << " | ";
    out << caps.max_senders_per_process << " | ";
    append_format_list(out, caps.requested_format_bits);
    out << " | ";
    append_format_list(out, caps.writable_storage_format_bits);
    out << " | ";
    append_format_list(out, caps.native_publish_format_bits);
    out << " | ";
    append_format_list(out, caps.direct_publish_format_bits);
    out << " | ";
    append_format_list(out, caps.cpu_read_format_bits);
    out << " | ";
    append_format_list(out, caps.cpu_write_format_bits);
    out << " |\n";
}

} // namespace

Result<backend_capabilities> get_backend_capabilities(backend_type backend) {
    const auto *caps = lookup_capabilities(backend);
    if (!caps) {
        return Error{ErrorCode::UnsupportedBackend, "unknown backend capability query"};
    }
    return *caps;
}

const char *backend_type_name(backend_type backend) noexcept {
    switch (backend) {
        case backend_type::d3d11: return "d3d11";
        case backend_type::metal: return "metal";
        case backend_type::opengl: return "opengl";
        case backend_type::dma_buf: return "dma_buf";
        case backend_type::unknown:
        default: return "unknown";
    }
}

namespace detail {

std::string backend_capabilities_markdown_matrix() {
    std::ostringstream out;
    out << "| backend | capability_flags | sharing_mechanisms | native_kind | max_senders_per_process | requested_formats | writable_storage_formats | native_publish_formats | direct_publish_formats | cpu_read_formats | cpu_write_formats |\n";
    out << "| --- | ---: | ---: | ---: | ---: | --- | --- | --- | --- | --- | --- |\n";
    append_capability_row(out, d3d11_capabilities);
    append_capability_row(out, metal_capabilities);
    append_capability_row(out, dma_buf_capabilities);
    append_capability_row(out, opengl_capabilities);
    return out.str();
}

} // namespace detail

} // namespace nozzle
