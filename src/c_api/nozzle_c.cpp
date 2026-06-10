// nozzle - nozzle_c.cpp - C ABI wrapper for nozzle public API

#include <nozzle/nozzle_c.h>

#include <nozzle/sender.hpp>
#include <nozzle/receiver.hpp>
#include <nozzle/frame.hpp>
#include <nozzle/texture.hpp>
#include <nozzle/device.hpp>
#include <nozzle/discovery.hpp>
#include <nozzle/pixel_access.hpp>
#include <nozzle/channel_swizzle.hpp>
#include <nozzle/format_convert.hpp>
#include <nozzle/format_resolve.hpp>
#include <nozzle/backend_capabilities.hpp>

#include "common/frame_helpers.hpp"

#if NOZZLE_HAS_OPENGL
#include <nozzle/backends/opengl.hpp>
#endif

#if NOZZLE_HAS_METAL
#include <nozzle/backends/metal.hpp>
#endif

#if NOZZLE_HAS_DMA_BUF
#include <nozzle/backends/linux.hpp>
#include "backends/linux/linux_helpers.hpp"
#include <drm_fourcc.h>
#endif

#include <cstdlib>
#include <cstddef>
#include <cstring>
#include <limits>
#include <memory>
#include <utility>

#include <plog/Log.h>
#include <new>

namespace {

#if NOZZLE_ENABLE_TEST_HOOKS
bool g_fail_next_writable_frame_wrapper_alloc = false;
bool g_fail_next_c_api_wrapper_object_alloc = false;
#endif


template <typename type_name, typename ... arguments>
NozzleErrorCode reset_unique_nothrow(
    std::unique_ptr<type_name> &target,
    arguments && ... args
) {
#if NOZZLE_ENABLE_TEST_HOOKS
    if (g_fail_next_c_api_wrapper_object_alloc) {
        g_fail_next_c_api_wrapper_object_alloc = false;
        return NOZZLE_ERROR_RESOURCE_CREATION_FAILED;
    }
#endif

    auto *value = new (std::nothrow) type_name(std::forward<arguments>(args) ...);
    if (!value) return NOZZLE_ERROR_RESOURCE_CREATION_FAILED;

    target.reset(value);
    return NOZZLE_OK;
}

NozzleErrorCode to_c_error(nozzle::ErrorCode code) {
    switch (code) {
        case nozzle::ErrorCode::Ok: return NOZZLE_OK;
        case nozzle::ErrorCode::Unknown: return NOZZLE_ERROR_UNKNOWN;
        case nozzle::ErrorCode::InvalidArgument: return NOZZLE_ERROR_INVALID_ARGUMENT;
        case nozzle::ErrorCode::UnsupportedBackend: return NOZZLE_ERROR_UNSUPPORTED_BACKEND;
        case nozzle::ErrorCode::UnsupportedFormat: return NOZZLE_ERROR_UNSUPPORTED_FORMAT;
        case nozzle::ErrorCode::DeviceMismatch: return NOZZLE_ERROR_DEVICE_MISMATCH;
        case nozzle::ErrorCode::ResourceCreationFailed: return NOZZLE_ERROR_RESOURCE_CREATION_FAILED;
        case nozzle::ErrorCode::SharedHandleFailed: return NOZZLE_ERROR_SHARED_HANDLE_FAILED;
        case nozzle::ErrorCode::SenderNotFound: return NOZZLE_ERROR_SENDER_NOT_FOUND;
        case nozzle::ErrorCode::SenderClosed: return NOZZLE_ERROR_SENDER_CLOSED;
        case nozzle::ErrorCode::Timeout: return NOZZLE_ERROR_TIMEOUT;
        case nozzle::ErrorCode::BackendError: return NOZZLE_ERROR_BACKEND_ERROR;
        case nozzle::ErrorCode::CommandFailed: return NOZZLE_ERROR_COMMAND_FAILED;
    }
    return NOZZLE_ERROR_UNKNOWN;
}

NozzleBackendType to_c_backend_type(nozzle::backend_type bt) {
    switch (bt) {
        case nozzle::backend_type::d3d11: return NOZZLE_BACKEND_D3D11;
        case nozzle::backend_type::metal: return NOZZLE_BACKEND_METAL;
        case nozzle::backend_type::opengl: return NOZZLE_BACKEND_OPENGL;
        case nozzle::backend_type::dma_buf: return NOZZLE_BACKEND_DMA_BUF;
        case nozzle::backend_type::unknown:
        default: return NOZZLE_BACKEND_UNKNOWN;
    }
}

nozzle::backend_type to_cpp_backend_type(NozzleBackendType bt) {
    switch (bt) {
        case NOZZLE_BACKEND_D3D11: return nozzle::backend_type::d3d11;
        case NOZZLE_BACKEND_METAL: return nozzle::backend_type::metal;
        case NOZZLE_BACKEND_OPENGL: return nozzle::backend_type::opengl;
        case NOZZLE_BACKEND_DMA_BUF: return nozzle::backend_type::dma_buf;
        case NOZZLE_BACKEND_UNKNOWN:
        default: return nozzle::backend_type::unknown;
    }
}

NozzleTextureFormat to_c_format(nozzle::texture_format fmt) {
    switch (fmt) {
        case nozzle::texture_format::r8_unorm: return NOZZLE_FORMAT_R8_UNORM;
        case nozzle::texture_format::rg8_unorm: return NOZZLE_FORMAT_RG8_UNORM;
        case nozzle::texture_format::rgb8_unorm: return NOZZLE_FORMAT_RGB8_UNORM;
        case nozzle::texture_format::rgba8_unorm: return NOZZLE_FORMAT_RGBA8_UNORM;
        case nozzle::texture_format::bgra8_unorm: return NOZZLE_FORMAT_BGRA8_UNORM;
        case nozzle::texture_format::rgba8_srgb: return NOZZLE_FORMAT_RGBA8_SRGB;
        case nozzle::texture_format::bgra8_srgb: return NOZZLE_FORMAT_BGRA8_SRGB;
        case nozzle::texture_format::r16_unorm: return NOZZLE_FORMAT_R16_UNORM;
        case nozzle::texture_format::rg16_unorm: return NOZZLE_FORMAT_RG16_UNORM;
        case nozzle::texture_format::rgb16_unorm: return NOZZLE_FORMAT_RGB16_UNORM;
        case nozzle::texture_format::rgba16_unorm: return NOZZLE_FORMAT_RGBA16_UNORM;
        case nozzle::texture_format::r16_float: return NOZZLE_FORMAT_R16_FLOAT;
        case nozzle::texture_format::rg16_float: return NOZZLE_FORMAT_RG16_FLOAT;
        case nozzle::texture_format::rgb16_float: return NOZZLE_FORMAT_RGB16_FLOAT;
        case nozzle::texture_format::rgba16_float: return NOZZLE_FORMAT_RGBA16_FLOAT;
        case nozzle::texture_format::r32_float: return NOZZLE_FORMAT_R32_FLOAT;
        case nozzle::texture_format::rg32_float: return NOZZLE_FORMAT_RG32_FLOAT;
        case nozzle::texture_format::rgb32_float: return NOZZLE_FORMAT_RGB32_FLOAT;
        case nozzle::texture_format::rgba32_float: return NOZZLE_FORMAT_RGBA32_FLOAT;
        case nozzle::texture_format::r32_uint: return NOZZLE_FORMAT_R32_UINT;
        case nozzle::texture_format::rgba32_uint: return NOZZLE_FORMAT_RGBA32_UINT;
        case nozzle::texture_format::rgb32_uint: return NOZZLE_FORMAT_RGB32_UINT;
        case nozzle::texture_format::depth32_float: return NOZZLE_FORMAT_DEPTH32_FLOAT;
        case nozzle::texture_format::unknown:
        default: return NOZZLE_FORMAT_UNKNOWN;
    }
}

nozzle::texture_format to_cpp_format(NozzleTextureFormat fmt) {
    switch (fmt) {
        case NOZZLE_FORMAT_R8_UNORM: return nozzle::texture_format::r8_unorm;
        case NOZZLE_FORMAT_RG8_UNORM: return nozzle::texture_format::rg8_unorm;
        case NOZZLE_FORMAT_RGB8_UNORM: return nozzle::texture_format::rgb8_unorm;
        case NOZZLE_FORMAT_RGBA8_UNORM: return nozzle::texture_format::rgba8_unorm;
        case NOZZLE_FORMAT_BGRA8_UNORM: return nozzle::texture_format::bgra8_unorm;
        case NOZZLE_FORMAT_RGBA8_SRGB: return nozzle::texture_format::rgba8_srgb;
        case NOZZLE_FORMAT_BGRA8_SRGB: return nozzle::texture_format::bgra8_srgb;
        case NOZZLE_FORMAT_R16_UNORM: return nozzle::texture_format::r16_unorm;
        case NOZZLE_FORMAT_RG16_UNORM: return nozzle::texture_format::rg16_unorm;
        case NOZZLE_FORMAT_RGB16_UNORM: return nozzle::texture_format::rgb16_unorm;
        case NOZZLE_FORMAT_RGBA16_UNORM: return nozzle::texture_format::rgba16_unorm;
        case NOZZLE_FORMAT_R16_FLOAT: return nozzle::texture_format::r16_float;
        case NOZZLE_FORMAT_RG16_FLOAT: return nozzle::texture_format::rg16_float;
        case NOZZLE_FORMAT_RGB16_FLOAT: return nozzle::texture_format::rgb16_float;
        case NOZZLE_FORMAT_RGBA16_FLOAT: return nozzle::texture_format::rgba16_float;
        case NOZZLE_FORMAT_R32_FLOAT: return nozzle::texture_format::r32_float;
        case NOZZLE_FORMAT_RG32_FLOAT: return nozzle::texture_format::rg32_float;
        case NOZZLE_FORMAT_RGB32_FLOAT: return nozzle::texture_format::rgb32_float;
        case NOZZLE_FORMAT_RGBA32_FLOAT: return nozzle::texture_format::rgba32_float;
        case NOZZLE_FORMAT_R32_UINT: return nozzle::texture_format::r32_uint;
        case NOZZLE_FORMAT_RGBA32_UINT: return nozzle::texture_format::rgba32_uint;
        case NOZZLE_FORMAT_RGB32_UINT: return nozzle::texture_format::rgb32_uint;
        case NOZZLE_FORMAT_DEPTH32_FLOAT: return nozzle::texture_format::depth32_float;
        default: return nozzle::texture_format::unknown;
    }
}

nozzle::receive_mode to_cpp_receive_mode(NozzleReceiveMode mode) {
    switch (mode) {
        case NOZZLE_RECEIVE_SEQUENTIAL_BEST_EFFORT:
            return nozzle::receive_mode::sequential_best_effort;
        case NOZZLE_RECEIVE_LATEST_ONLY:
        default:
            return nozzle::receive_mode::latest_only;
    }
}

NozzleTextureOrigin to_c_origin(nozzle::texture_origin origin) {
    switch (origin) {
        case nozzle::texture_origin::top_left: return NOZZLE_ORIGIN_TOP_LEFT;
        case nozzle::texture_origin::bottom_left: return NOZZLE_ORIGIN_BOTTOM_LEFT;
    }
    return NOZZLE_ORIGIN_TOP_LEFT;
}

nozzle::texture_origin to_cpp_origin(NozzleTextureOrigin origin) {
    switch (origin) {
        case NOZZLE_ORIGIN_TOP_LEFT: return nozzle::texture_origin::top_left;
        case NOZZLE_ORIGIN_BOTTOM_LEFT: return nozzle::texture_origin::bottom_left;
    }
    return nozzle::texture_origin::top_left;
}

nozzle::channel_swizzle to_cpp_swizzle(NozzleChannelSwizzle swizzle) {
    switch (swizzle) {
        case NOZZLE_CHANNEL_SWIZZLE_SWAP_RB:
            return nozzle::channel_swizzle::swap_rb;
        case NOZZLE_CHANNEL_SWIZZLE_IDENTITY:
        default:
            return nozzle::channel_swizzle::identity;
    }
}

NozzleFormatSource to_c_format_source(nozzle::format_source src) {
    switch (src) {
        case nozzle::format_source::requested: return NOZZLE_FORMAT_SOURCE_REQUESTED;
        case nozzle::format_source::caller_hint: return NOZZLE_FORMAT_SOURCE_CALLER_HINT;
        case nozzle::format_source::native_observed: return NOZZLE_FORMAT_SOURCE_NATIVE_OBSERVED;
        case nozzle::format_source::registry_metadata: return NOZZLE_FORMAT_SOURCE_UNKNOWN;
    }
    return NOZZLE_FORMAT_SOURCE_UNKNOWN;
}

NozzleNativeFormatKind to_c_native_kind(nozzle::native_format_kind kind) {
    switch (kind) {
        case nozzle::native_format_kind::mtl_pixel_format: return NOZZLE_NATIVE_KIND_MTL_PIXEL_FORMAT;
        case nozzle::native_format_kind::dxgi_format: return NOZZLE_NATIVE_KIND_DXGI_FORMAT;
        case nozzle::native_format_kind::drm_fourcc: return NOZZLE_NATIVE_KIND_DRM_FOURCC;
        case nozzle::native_format_kind::gl_internal_format: return NOZZLE_NATIVE_KIND_GL_INTERNAL_FORMAT;
        case nozzle::native_format_kind::unknown:
        default: return NOZZLE_NATIVE_KIND_UNKNOWN;
    }
}

NozzleTransferMode to_c_transfer_mode(nozzle::transfer_mode tm) {
    switch (tm) {
        case nozzle::transfer_mode::zero_copy_shared_texture: return NOZZLE_TRANSFER_ZERO_COPY_SHARED_TEXTURE;
        case nozzle::transfer_mode::gpu_copy: return NOZZLE_TRANSFER_GPU_COPY;
        case nozzle::transfer_mode::cpu_copy: return NOZZLE_TRANSFER_CPU_COPY;
        case nozzle::transfer_mode::unknown:
        default: return NOZZLE_TRANSFER_UNKNOWN;
    }
}

NozzleSyncMode to_c_sync_mode(nozzle::sync_mode sm) {
    switch (sm) {
        case nozzle::sync_mode::access_guarded: return NOZZLE_SYNC_ACCESS_GUARDED;
        case nozzle::sync_mode::gpu_fence_best_effort: return NOZZLE_SYNC_GPU_FENCE_BEST_EFFORT;
        case nozzle::sync_mode::none:
        default: return NOZZLE_SYNC_NONE;
    }
}

static_assert(static_cast<uint8_t>(nozzle::fallback_category::none) == NOZZLE_FALLBACK_CATEGORY_NONE, "");
static_assert(static_cast<uint8_t>(nozzle::fallback_category::storage_compatible) == NOZZLE_FALLBACK_CATEGORY_STORAGE_COMPATIBLE, "");
static_assert(static_cast<uint8_t>(nozzle::fallback_category::channel_expansion) == NOZZLE_FALLBACK_CATEGORY_CHANNEL_EXPANSION, "");
static_assert(static_cast<uint8_t>(nozzle::fallback_category::quality_loss) == NOZZLE_FALLBACK_CATEGORY_QUALITY_LOSS, "");

static_assert(static_cast<uint8_t>(nozzle::channel_swizzle::identity) == NOZZLE_CHANNEL_SWIZZLE_IDENTITY, "");
static_assert(static_cast<uint8_t>(nozzle::channel_swizzle::swap_rb) == NOZZLE_CHANNEL_SWIZZLE_SWAP_RB, "");

static_assert(sizeof(NozzleFormatFallbackInfo) == 24, "");
static_assert(alignof(NozzleFormatFallbackInfo) == 4, "");
static_assert(offsetof(NozzleFormatFallbackInfo, requested_format) == 0, "");
static_assert(offsetof(NozzleFormatFallbackInfo, storage_format) == 4, "");
static_assert(offsetof(NozzleFormatFallbackInfo, fallback_target) == 8, "");
static_assert(offsetof(NozzleFormatFallbackInfo, category) == 12, "");
static_assert(offsetof(NozzleFormatFallbackInfo, swizzle) == 16, "");
static_assert(offsetof(NozzleFormatFallbackInfo, quality_loss) == 20, "");

static_assert(NOZZLE_BACKEND_CAPABILITIES_VERSION == nozzle::backend_capabilities_version, "");
static_assert(NOZZLE_SHARING_IOSURFACE == static_cast<uint32_t>(nozzle::backend_sharing_mechanism::iosurface), "");
static_assert(NOZZLE_SHARING_D3D11_NT_HANDLE == static_cast<uint32_t>(nozzle::backend_sharing_mechanism::d3d11_nt_handle), "");
static_assert(NOZZLE_SHARING_DMA_BUF == static_cast<uint32_t>(nozzle::backend_sharing_mechanism::dma_buf), "");
static_assert(NOZZLE_SHARING_OPENGL_TEXTURE == static_cast<uint32_t>(nozzle::backend_sharing_mechanism::opengl_texture), "");
static_assert(NOZZLE_BACKEND_CAP_SENDER == static_cast<uint32_t>(nozzle::backend_capability_flags::sender), "");
static_assert(NOZZLE_BACKEND_CAP_RECEIVER == static_cast<uint32_t>(nozzle::backend_capability_flags::receiver), "");
static_assert(NOZZLE_BACKEND_CAP_WRITABLE_FRAMES == static_cast<uint32_t>(nozzle::backend_capability_flags::writable_frames), "");
static_assert(NOZZLE_BACKEND_CAP_NATIVE_TEXTURE_PUBLISH == static_cast<uint32_t>(nozzle::backend_capability_flags::native_texture_publish), "");
static_assert(NOZZLE_BACKEND_CAP_DIRECT_EXTERNAL_PUBLISH == static_cast<uint32_t>(nozzle::backend_capability_flags::direct_external_publish), "");
static_assert(NOZZLE_BACKEND_CAP_CPU_READ == static_cast<uint32_t>(nozzle::backend_capability_flags::cpu_read), "");
static_assert(NOZZLE_BACKEND_CAP_CPU_WRITE == static_cast<uint32_t>(nozzle::backend_capability_flags::cpu_write), "");
static_assert(NOZZLE_BACKEND_CAP_ZERO_COPY_RECEIVE == static_cast<uint32_t>(nozzle::backend_capability_flags::zero_copy_receive), "");
static_assert(NOZZLE_BACKEND_CAP_ZERO_COPY_PUBLISH == static_cast<uint32_t>(nozzle::backend_capability_flags::zero_copy_publish), "");
static_assert(NOZZLE_BACKEND_CAP_REQUIRES_MATCHING_BACKEND == static_cast<uint32_t>(nozzle::backend_capability_flags::requires_matching_backend), "");
static_assert(NOZZLE_BACKEND_CAP_SINGLE_SENDER_PER_PROCESS == static_cast<uint32_t>(nozzle::backend_capability_flags::single_sender_per_process), "");
static_assert(NOZZLE_BACKEND_CAP_MAY_REQUIRE_RUNTIME_PROBE == static_cast<uint32_t>(nozzle::backend_capability_flags::may_require_runtime_probe), "");

static_assert(sizeof(NozzleBackendCapabilities) == 88, "");
static_assert(alignof(NozzleBackendCapabilities) == 8, "");
static_assert(offsetof(NozzleBackendCapabilities, struct_size) == 0, "");
static_assert(offsetof(NozzleBackendCapabilities, version) == 4, "");
static_assert(offsetof(NozzleBackendCapabilities, backend) == 8, "");
static_assert(offsetof(NozzleBackendCapabilities, capability_flags) == 12, "");
static_assert(offsetof(NozzleBackendCapabilities, sharing_mechanisms) == 16, "");
static_assert(offsetof(NozzleBackendCapabilities, native_format_kind) == 20, "");
static_assert(offsetof(NozzleBackendCapabilities, default_fallback_flags) == 24, "");
static_assert(offsetof(NozzleBackendCapabilities, max_senders_per_process) == 28, "");
static_assert(offsetof(NozzleBackendCapabilities, requested_format_bits) == 32, "");
static_assert(offsetof(NozzleBackendCapabilities, writable_storage_format_bits) == 40, "");
static_assert(offsetof(NozzleBackendCapabilities, native_publish_format_bits) == 48, "");
static_assert(offsetof(NozzleBackendCapabilities, direct_publish_format_bits) == 56, "");
static_assert(offsetof(NozzleBackendCapabilities, cpu_read_format_bits) == 64, "");
static_assert(offsetof(NozzleBackendCapabilities, cpu_write_format_bits) == 72, "");
static_assert(offsetof(NozzleBackendCapabilities, known_quality_loss_format_bits) == 80, "");

NozzleErrorCode fill_fallback(
    NozzleFormatFallbackInfo *out_info,
    const nozzle::format_fallback_info &fb
) {
    if (!out_info) return NOZZLE_ERROR_INVALID_ARGUMENT;

    out_info->requested_format = to_c_format(fb.requested_format);
    out_info->storage_format = to_c_format(fb.storage_format);
    out_info->fallback_target = to_c_format(fb.fallback_target);
    out_info->category = static_cast<NozzleFallbackCategory>(fb.category);
    out_info->swizzle = static_cast<NozzleChannelSwizzle>(fb.swizzle);
    out_info->quality_loss = fb.quality_loss ? 1 : 0;
    return NOZZLE_OK;
}

NozzleErrorCode validate_dmabuf_publish_desc(
    const NozzleDmaBufPublishDesc *desc
) {
    if (!desc) return NOZZLE_ERROR_INVALID_ARGUMENT;
    if (desc->dmabuf_fd < 0) return NOZZLE_ERROR_INVALID_ARGUMENT;
    if (desc->width == 0 || desc->height == 0) return NOZZLE_ERROR_INVALID_ARGUMENT;
    if (desc->drm_fourcc == 0) return NOZZLE_ERROR_INVALID_ARGUMENT;
    if (desc->plane_count != 1) return NOZZLE_ERROR_UNSUPPORTED_FORMAT;
    if (desc->planes[0].stride == 0) return NOZZLE_ERROR_INVALID_ARGUMENT;
    if (desc->storage_format == NOZZLE_FORMAT_UNKNOWN ||
        desc->semantic_format == NOZZLE_FORMAT_UNKNOWN) {
        return NOZZLE_ERROR_UNSUPPORTED_FORMAT;
    }
#if NOZZLE_HAS_DMA_BUF
    if (desc->drm_fourcc == DRM_FORMAT_INVALID) return NOZZLE_ERROR_INVALID_ARGUMENT;

    uint32_t expected_fourcc = nozzle::detail::linux_backend::drm_format_from_nozzle(
        static_cast<uint32_t>(to_cpp_format(desc->storage_format)));
    if (expected_fourcc == 0 ||
        expected_fourcc == DRM_FORMAT_INVALID ||
        expected_fourcc != desc->drm_fourcc) {
        return NOZZLE_ERROR_UNSUPPORTED_FORMAT;
    }
#endif
    return NOZZLE_OK;
}

bool checked_abs_i64_to_u64(int64_t value, uint64_t *out_abs_value) {
    if (!out_abs_value) return false;
    if (value == std::numeric_limits<int64_t>::min()) return false;

    if (value < 0) {
        *out_abs_value = static_cast<uint64_t>(-value);
    } else {
        *out_abs_value = static_cast<uint64_t>(value);
    }
    return true;
}

void fill_c_mapped_pixels(
    const nozzle::mapped_pixels &mapped,
    NozzleMappedPixels *out_pixels
) {
    if (!out_pixels) {
        return;
    }
    out_pixels->data = mapped.data;
    out_pixels->row_stride_bytes = mapped.row_stride_bytes;
    out_pixels->width = mapped.width;
    out_pixels->height = mapped.height;
    out_pixels->format = to_c_format(mapped.format);
    out_pixels->origin = to_c_origin(mapped.origin);
}

NozzleErrorCode copy_mapped_pixels_to_buffer(
    const nozzle::mapped_pixels &mapped,
    void *out_data,
    uint64_t out_data_size_bytes,
    NozzleMappedPixels *out_pixels
) {
    if (!out_data || !out_pixels) return NOZZLE_ERROR_INVALID_ARGUMENT;
    if (!mapped.data) return NOZZLE_ERROR_BACKEND_ERROR;
    if (mapped.cpu_layout.bytes_per_pixel == 0) return NOZZLE_ERROR_UNSUPPORTED_FORMAT;

    const uint64_t bytes_per_pixel = mapped.cpu_layout.bytes_per_pixel;
    const uint64_t row_copy_bytes =
        static_cast<uint64_t>(mapped.width) * bytes_per_pixel;
    if (mapped.height != 0 &&
        row_copy_bytes > std::numeric_limits<uint64_t>::max() / mapped.height) {
        return NOZZLE_ERROR_INVALID_ARGUMENT;
    }

    const uint64_t required_size = row_copy_bytes * mapped.height;
    if (out_data_size_bytes < required_size) return NOZZLE_ERROR_INVALID_ARGUMENT;

    const int64_t source_stride = static_cast<int64_t>(mapped.row_stride_bytes);
    uint64_t source_abs_stride{0};
    if (!checked_abs_i64_to_u64(source_stride, &source_abs_stride)) {
        return NOZZLE_ERROR_INVALID_ARGUMENT;
    }
    if (source_abs_stride < row_copy_bytes) return NOZZLE_ERROR_BACKEND_ERROR;
    if (row_copy_bytes > static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) {
        return NOZZLE_ERROR_INVALID_ARGUMENT;
    }

    const auto *source_base = static_cast<const uint8_t *>(mapped.data);
    auto *destination_base = static_cast<uint8_t *>(out_data);
    const std::size_t copy_size = static_cast<std::size_t>(row_copy_bytes);

    for (uint32_t y = 0; y < mapped.height; ++y) {
        const uint8_t *source_row = source_stride < 0
            ? source_base - static_cast<uint64_t>(y) * source_abs_stride
            : source_base + static_cast<uint64_t>(y) * source_abs_stride;
        uint8_t *destination_row = destination_base + static_cast<uint64_t>(y) * row_copy_bytes;
        std::memcpy(destination_row, source_row, copy_size);
    }

    fill_c_mapped_pixels(mapped, out_pixels);
    out_pixels->data = out_data;
    out_pixels->row_stride_bytes = static_cast<int64_t>(row_copy_bytes);
    return NOZZLE_OK;
}

} // anonymous namespace

#include "nozzle_c_types.hpp"

// ========== Sender API ==========

extern "C" {

NozzleErrorCode nozzle_resolve_fallback_flags(
    const NozzleSenderDesc *desc,
    uint32_t *out_flags
) {
    if (!desc || !out_flags) return NOZZLE_ERROR_INVALID_ARGUMENT;

    if (desc->fallback_flags_valid != 0) {
        auto vr = nozzle::validate_fallback_flags(desc->fallback_flags);
        if (!vr.ok()) return to_c_error(vr.error().code);
        *out_flags = desc->fallback_flags;
    } else {
        if (desc->allow_format_fallback != 0) {
            LOG_WARNING << "allow_format_fallback is deprecated and enables "
                << "storage-compatible, channel-expansion, and quality-loss fallback. "
                << "Set fallback_flags_valid=1 and fallback_flags explicitly.";
            *out_flags = NOZZLE_FALLBACK_SAFE_DEFAULTS | NOZZLE_FALLBACK_QUALITY_LOSS;
        } else {
            *out_flags = NOZZLE_FALLBACK_NONE;
        }
    }
    return NOZZLE_OK;
}

NozzleErrorCode nozzle_get_backend_capabilities(
    NozzleBackendType backend,
    NozzleBackendCapabilities *out_caps
) {
    if (!out_caps) return NOZZLE_ERROR_INVALID_ARGUMENT;
    uint32_t caller_struct_size = out_caps->struct_size;
    if (caller_struct_size != 0 && caller_struct_size != sizeof(NozzleBackendCapabilities)) {
        return NOZZLE_ERROR_INVALID_ARGUMENT;
    }

    auto result = nozzle::get_backend_capabilities(to_cpp_backend_type(backend));
    if (!result.ok()) {
        std::memset(out_caps, 0, sizeof(*out_caps));
        return to_c_error(result.error().code);
    }

    const auto &caps = result.value();
    std::memset(out_caps, 0, sizeof(*out_caps));
    out_caps->struct_size = sizeof(*out_caps);
    out_caps->version = caps.version;
    out_caps->backend = to_c_backend_type(caps.backend);
    out_caps->capability_flags = caps.capability_flags;
    out_caps->sharing_mechanisms = caps.sharing_mechanisms;
    out_caps->native_format_kind = to_c_native_kind(caps.native_kind);
    out_caps->default_fallback_flags = caps.default_fallback_flags;
    out_caps->max_senders_per_process = caps.max_senders_per_process;
    out_caps->requested_format_bits = caps.requested_format_bits;
    out_caps->writable_storage_format_bits = caps.writable_storage_format_bits;
    out_caps->native_publish_format_bits = caps.native_publish_format_bits;
    out_caps->direct_publish_format_bits = caps.direct_publish_format_bits;
    out_caps->cpu_read_format_bits = caps.cpu_read_format_bits;
    out_caps->cpu_write_format_bits = caps.cpu_write_format_bits;
    out_caps->known_quality_loss_format_bits = caps.known_quality_loss_format_bits;
    return NOZZLE_OK;
}

int nozzle_backend_is_available(NozzleBackendType backend) {
    return nozzle::is_backend_available(to_cpp_backend_type(backend)) ? 1 : 0;
}

int nozzle_backend_capabilities_support_format(
    const NozzleBackendCapabilities *caps,
    NozzleTextureFormat format,
    uint64_t format_bits
) {
    if (!caps) return 0;
    if (caps->struct_size != sizeof(NozzleBackendCapabilities)) return 0;
    if (caps->version != NOZZLE_BACKEND_CAPABILITIES_VERSION) return 0;
    (void)caps;
    return nozzle::supports_format(format_bits, to_cpp_format(format)) ? 1 : 0;
}

NozzleErrorCode nozzle_sender_create(
    const NozzleSenderDesc *desc,
    NozzleSender **out_sender
) {
    if (!desc || !out_sender) return NOZZLE_ERROR_INVALID_ARGUMENT;
    *out_sender = nullptr;

    uint32_t fb_flags{0};
    NozzleErrorCode flag_ec = nozzle_resolve_fallback_flags(desc, &fb_flags);
    if (flag_ec != NOZZLE_OK) return flag_ec;

    nozzle::sender_desc cpp_desc{};
    if (desc->name) cpp_desc.name = desc->name;
    if (desc->application_name) cpp_desc.application_name = desc->application_name;
    cpp_desc.ring_buffer_size = desc->ring_buffer_size;
    cpp_desc.fallback_flags = fb_flags;

    auto result = nozzle::sender::create(cpp_desc);
    if (!result.ok()) return to_c_error(result.error().code);

    auto *wrapper = new (std::nothrow) NozzleSender{};
    if (!wrapper) return NOZZLE_ERROR_RESOURCE_CREATION_FAILED;

    NozzleErrorCode object_error = reset_unique_nothrow(
        wrapper->obj, std::move(result.value()));
    if (object_error != NOZZLE_OK) {
        delete wrapper;
        return object_error;
    }

    *out_sender = wrapper;
    return NOZZLE_OK;
}

NozzleErrorCode nozzle_sender_create_with_native_device(
    const NozzleSenderDesc *desc,
    const NozzleNativeDevice *native_device,
    NozzleSender **out_sender
) {
    if (!desc || !out_sender || !native_device) return NOZZLE_ERROR_INVALID_ARGUMENT;
    *out_sender = nullptr;
    if (!native_device->device) return NOZZLE_ERROR_INVALID_ARGUMENT;

    uint32_t fb_flags{0};
    NozzleErrorCode flag_ec = nozzle_resolve_fallback_flags(desc, &fb_flags);
    if (flag_ec != NOZZLE_OK) return flag_ec;

    nozzle::sender_desc cpp_desc{};
    if (desc->name) cpp_desc.name = desc->name;
    if (desc->application_name) cpp_desc.application_name = desc->application_name;
    cpp_desc.ring_buffer_size = desc->ring_buffer_size;
    cpp_desc.fallback_flags = fb_flags;
    cpp_desc.native_device.backend = to_cpp_backend_type(native_device->backend);
    cpp_desc.native_device.device = native_device->device;
    cpp_desc.native_device.context = native_device->context;

    auto result = nozzle::sender::create(cpp_desc);
    if (!result.ok()) return to_c_error(result.error().code);

    auto *wrapper = new (std::nothrow) NozzleSender{};
    if (!wrapper) return NOZZLE_ERROR_RESOURCE_CREATION_FAILED;

    NozzleErrorCode object_error = reset_unique_nothrow(
        wrapper->obj, std::move(result.value()));
    if (object_error != NOZZLE_OK) {
        delete wrapper;
        return object_error;
    }

    *out_sender = wrapper;
    return NOZZLE_OK;
}

void nozzle_sender_destroy(NozzleSender *sender) {
    delete sender;
}

NozzleErrorCode nozzle_sender_publish_texture(
    NozzleSender *sender,
    NozzleTexture *texture
) {
    if (!sender || !texture) return NOZZLE_ERROR_INVALID_ARGUMENT;

    auto result = sender->obj->publish_external_texture(*texture->obj);
    if (!result.ok()) return to_c_error(result.error().code);
    return NOZZLE_OK;
}

NozzleErrorCode nozzle_sender_acquire_writable_frame(
    NozzleSender *sender,
    uint32_t width,
    uint32_t height,
    NozzleTextureFormat format,
    NozzleFrame **out_frame
) {
    if (!sender || !out_frame) return NOZZLE_ERROR_INVALID_ARGUMENT;
    *out_frame = nullptr;

    nozzle::texture_desc td{};
    td.width = width;
    td.height = height;
    td.format = to_cpp_format(format);

    auto result = sender->obj->acquire_writable_frame(td);
    if (!result.ok()) return to_c_error(result.error().code);

    auto writable = std::move(result.value());

#if NOZZLE_ENABLE_TEST_HOOKS
    if (g_fail_next_writable_frame_wrapper_alloc) {
        g_fail_next_writable_frame_wrapper_alloc = false;
        (void)sender->obj->discard_frame(writable);
        return NOZZLE_ERROR_RESOURCE_CREATION_FAILED;
    }
#endif

    auto *wrapper = new (std::nothrow) NozzleFrame{};
    if (!wrapper) {
        (void)sender->obj->discard_frame(writable);
        return NOZZLE_ERROR_RESOURCE_CREATION_FAILED;
    }

    auto *writable_wrapper = new (std::nothrow) nozzle::writable_frame();
    if (!writable_wrapper) {
        (void)sender->obj->discard_frame(writable);
        delete wrapper;
        return NOZZLE_ERROR_RESOURCE_CREATION_FAILED;
    }
    *writable_wrapper = std::move(writable);

    wrapper->writable.reset(writable_wrapper);
    wrapper->is_writable = true;
    *out_frame = wrapper;
    return NOZZLE_OK;
}

NozzleErrorCode nozzle_sender_commit_frame(
    NozzleSender *sender,
    NozzleFrame *frame
) {
    if (!sender || !frame || !frame->is_writable) return NOZZLE_ERROR_INVALID_ARGUMENT;

    auto result = sender->obj->commit_frame(*frame->writable);
    if (!result.ok()) return to_c_error(result.error().code);
    return NOZZLE_OK;
}

NozzleErrorCode nozzle_sender_discard_frame(
    NozzleSender *sender,
    NozzleFrame *frame
) {
    if (!sender || !frame || !frame->is_writable) return NOZZLE_ERROR_INVALID_ARGUMENT;

    auto result = sender->obj->discard_frame(*frame->writable);
    if (!result.ok()) return to_c_error(result.error().code);
    return NOZZLE_OK;
}

NozzleErrorCode nozzle_sender_get_info(
    NozzleSender *sender,
    NozzleSenderInfo *out_info
) {
    if (!sender || !out_info) return NOZZLE_ERROR_INVALID_ARGUMENT;

    sender->cached_info = sender->obj->info();
    out_info->name = sender->cached_info.name.c_str();
    out_info->application_name = sender->cached_info.application_name.c_str();
    out_info->id = sender->cached_info.id.c_str();
    out_info->backend = to_c_backend_type(sender->cached_info.backend);
    return NOZZLE_OK;
}

NozzleErrorCode nozzle_sender_get_native_device(
    NozzleSender *sender,
    NozzleNativeDevice *out_device
) {
    if (!sender || !out_device) return NOZZLE_ERROR_INVALID_ARGUMENT;

    auto nd = sender->obj->native_device();
    out_device->backend = to_c_backend_type(nd.backend);
    out_device->device = nd.device;
    out_device->context = nd.context;
    return NOZZLE_OK;
}

// ========== Receiver API ==========

NozzleErrorCode nozzle_receiver_create(
    const NozzleReceiverDesc *desc,
    NozzleReceiver **out_receiver
) {
    if (!desc || !out_receiver) return NOZZLE_ERROR_INVALID_ARGUMENT;
    *out_receiver = nullptr;

    nozzle::receiver_desc cpp_desc{};
    if (desc->name) cpp_desc.name = desc->name;
    if (desc->application_name) cpp_desc.application_name = desc->application_name;
    cpp_desc.receive_mode_val = to_cpp_receive_mode(desc->receive_mode);

    auto result = nozzle::receiver::create(cpp_desc);
    if (!result.ok()) return to_c_error(result.error().code);

    auto *wrapper = new (std::nothrow) NozzleReceiver{};
    if (!wrapper) return NOZZLE_ERROR_RESOURCE_CREATION_FAILED;

    NozzleErrorCode object_error = reset_unique_nothrow(
        wrapper->obj, std::move(result.value()));
    if (object_error != NOZZLE_OK) {
        delete wrapper;
        return object_error;
    }

    *out_receiver = wrapper;
    return NOZZLE_OK;
}

void nozzle_receiver_destroy(NozzleReceiver *receiver) {
    delete receiver;
}

NozzleErrorCode nozzle_receiver_acquire_frame(
    NozzleReceiver *receiver,
    const NozzleAcquireDesc *desc,
    NozzleFrame **out_frame
) {
    if (!receiver || !out_frame) return NOZZLE_ERROR_INVALID_ARGUMENT;

    auto make_frame_wrapper = [](nozzle::frame &&f, NozzleFrame **out_wrapper) -> NozzleErrorCode {
        auto *w = new (std::nothrow) NozzleFrame{};
        if (!w) return NOZZLE_ERROR_RESOURCE_CREATION_FAILED;

        NozzleErrorCode object_error = reset_unique_nothrow(w->obj, std::move(f));
        if (object_error != NOZZLE_OK) {
            delete w;
            return object_error;
        }

        w->is_writable = false;
        *out_wrapper = w;
        return NOZZLE_OK;
    };

    *out_frame = nullptr;
    NozzleFrame *wrapper = nullptr;

    if (desc) {
        nozzle::acquire_desc ad{};
        ad.timeout_ms = desc->timeout_ms;
        auto result = receiver->obj->acquire_frame(ad);
        if (!result.ok()) return to_c_error(result.error().code);
        NozzleErrorCode wrapper_error = make_frame_wrapper(
            std::move(result.value()), &wrapper);
        if (wrapper_error != NOZZLE_OK) return wrapper_error;
    } else {
        auto result = receiver->obj->acquire_frame();
        if (!result.ok()) return to_c_error(result.error().code);
        NozzleErrorCode wrapper_error = make_frame_wrapper(
            std::move(result.value()), &wrapper);
        if (wrapper_error != NOZZLE_OK) return wrapper_error;
    }

    *out_frame = wrapper;
    return NOZZLE_OK;
}

void nozzle_frame_release(NozzleFrame *frame) {
    delete frame;
}

NozzleErrorCode nozzle_receiver_get_connected_info(
    NozzleReceiver *receiver,
    NozzleConnectedSenderInfo *out_info
) {
    if (!receiver || !out_info) return NOZZLE_ERROR_INVALID_ARGUMENT;

    receiver->cached_connected_info = receiver->obj->connected_info();
    const auto &ci = receiver->cached_connected_info;
    out_info->name = ci.name.c_str();
    out_info->application_name = ci.application_name.c_str();
    out_info->id = ci.id.c_str();
    out_info->backend = to_c_backend_type(ci.backend);
    out_info->width = ci.width;
    out_info->height = ci.height;
    out_info->format = to_c_format(ci.format);
    out_info->semantic_format = to_c_format(ci.semantic_format);
    out_info->estimated_fps = ci.estimated_fps;
    out_info->frame_counter = ci.frame_counter;
    out_info->last_update_time_ns = ci.last_update_time_ns;
    out_info->native_format_kind = to_c_native_kind(static_cast<nozzle::native_format_kind>(ci.native_format_kind));
    out_info->native_format_value = ci.native_format_value;
    out_info->native_format_modifier = ci.native_format_modifier;
    return NOZZLE_OK;
}

NozzleErrorCode nozzle_frame_get_info(
    NozzleFrame *frame,
    NozzleFrameInfo *out_info
) {
    if (!frame || !out_info) return NOZZLE_ERROR_INVALID_ARGUMENT;

    if (frame->is_writable) {
        const auto &td = frame->writable->desc();
        out_info->frame_index = 0;
        out_info->timestamp_ns = 0;
        out_info->width = td.width;
        out_info->height = td.height;
        out_info->format = to_c_format(td.format);
        out_info->semantic_format = to_c_format(td.semantic_format);
        out_info->transfer_mode = NOZZLE_TRANSFER_UNKNOWN;
        out_info->sync_mode = NOZZLE_SYNC_NONE;
        out_info->dropped_frame_count = 0;
    } else {
        const auto fi = frame->obj->info();
        out_info->frame_index = fi.frame_index;
        out_info->timestamp_ns = fi.timestamp_ns;
        out_info->width = fi.width;
        out_info->height = fi.height;
        out_info->format = to_c_format(fi.format);
        out_info->semantic_format = to_c_format(fi.semantic_format);
        out_info->transfer_mode = to_c_transfer_mode(fi.transfer_mode_val);
        out_info->sync_mode = to_c_sync_mode(fi.sync_mode_val);
        out_info->dropped_frame_count = fi.dropped_frame_count;
    }
    return NOZZLE_OK;
}

NozzleErrorCode nozzle_frame_get_format_fallback_info(
    NozzleFrame *frame,
    NozzleFormatFallbackInfo *out_info
) {
    if (!frame || !out_info) return NOZZLE_ERROR_INVALID_ARGUMENT;

    nozzle::format_fallback_info fb;
    if (frame->is_writable) {
        fb = {};
    } else {
        fb = frame->obj->info().fallback;
    }

    return fill_fallback(out_info, fb);
}

NozzleErrorCode nozzle_receiver_get_connected_format_fallback_info(
    NozzleReceiver *receiver,
    NozzleFormatFallbackInfo *out_info
) {
    if (!receiver || !out_info) return NOZZLE_ERROR_INVALID_ARGUMENT;

#if NOZZLE_ENABLE_TEST_HOOKS
    if (receiver->use_cached_connected_info_for_tests) {
        return fill_fallback(out_info, receiver->cached_connected_info.fallback);
    }
#endif

    if (!receiver->obj) return NOZZLE_ERROR_INVALID_ARGUMENT;

    receiver->cached_connected_info = receiver->obj->connected_info();
    return fill_fallback(out_info, receiver->cached_connected_info.fallback);
}

// ========== Discovery ==========

NozzleErrorCode nozzle_enumerate_senders(
    NozzleSenderInfoArray *out_array
) {
    if (!out_array) return NOZZLE_ERROR_INVALID_ARGUMENT;

    auto senders = nozzle::enumerate_senders();
    auto count = static_cast<uint32_t>(senders.size());

    if (count == 0) {
        out_array->items = nullptr;
        out_array->count = 0;
        return NOZZLE_OK;
    }

    std::size_t string_storage = 0;
    for (const auto &s : senders) {
        string_storage += s.name.size() + 1;
        string_storage += s.application_name.size() + 1;
        string_storage += s.id.size() + 1;
    }

    std::size_t infos_size = count * sizeof(NozzleSenderInfo);
    std::size_t total = infos_size + string_storage;

    void *buf = std::malloc(total);
    if (!buf) return NOZZLE_ERROR_RESOURCE_CREATION_FAILED;

    auto *infos = static_cast<NozzleSenderInfo *>(buf);
    char *str_ptr = static_cast<char *>(buf) + infos_size;

    for (uint32_t i = 0; i < count; ++i) {
        const auto &s = senders[i];
        infos[i].backend = to_c_backend_type(s.backend);

        std::memcpy(str_ptr, s.name.c_str(), s.name.size() + 1);
        infos[i].name = str_ptr;
        str_ptr += s.name.size() + 1;

        std::memcpy(str_ptr, s.application_name.c_str(), s.application_name.size() + 1);
        infos[i].application_name = str_ptr;
        str_ptr += s.application_name.size() + 1;

        std::memcpy(str_ptr, s.id.c_str(), s.id.size() + 1);
        infos[i].id = str_ptr;
        str_ptr += s.id.size() + 1;
    }

    out_array->items = infos;
    out_array->count = count;
    return NOZZLE_OK;
}

void nozzle_free_sender_info_array(NozzleSenderInfoArray *array) {
    if (!array) return;
    std::free(array->items);
    array->items = nullptr;
    array->count = 0;
}

// ========== Device API ==========

NozzleErrorCode nozzle_device_get_default(
    NozzleDevice **out_device
) {
    if (!out_device) return NOZZLE_ERROR_INVALID_ARGUMENT;

    *out_device = nullptr;

    auto result = nozzle::device::default_device();
    if (!result.ok()) return to_c_error(result.error().code);

    auto *wrapper = new (std::nothrow) NozzleDevice{};
    if (!wrapper) return NOZZLE_ERROR_RESOURCE_CREATION_FAILED;

    NozzleErrorCode object_error = reset_unique_nothrow(
        wrapper->obj, std::move(result.value()));
    if (object_error != NOZZLE_OK) {
        delete wrapper;
        return object_error;
    }

    *out_device = wrapper;
    return NOZZLE_OK;
}

void nozzle_device_destroy(NozzleDevice *device) {
    delete device;
}

// ========== Pixel Access (CPU) ==========

NozzleErrorCode nozzle_frame_get_resolved_format(
    NozzleFrame *frame,
    NozzleResolvedTextureFormat *out_resolved
) {
    if (!frame || !out_resolved) return NOZZLE_ERROR_INVALID_ARGUMENT;

    const nozzle::resolved_texture_format *resolved = nullptr;
    nozzle::resolved_texture_format tmp;

    if (frame->is_writable) {
        if (!frame->writable) return NOZZLE_ERROR_INVALID_ARGUMENT;
        tmp = frame->writable->get_texture().resolved();
        resolved = &tmp;
    } else {
        if (!frame->obj) return NOZZLE_ERROR_INVALID_ARGUMENT;
        tmp = frame->obj->get_texture().resolved();
        resolved = &tmp;
    }

    std::memset(out_resolved, 0, sizeof(NozzleResolvedTextureFormat));
    out_resolved->storage_format = to_c_format(resolved->storage_format);
    out_resolved->semantic_format = to_c_format(resolved->semantic_format);
    out_resolved->format_source = to_c_format_source(resolved->source);
    out_resolved->native_backend = to_c_backend_type(resolved->native.backend);
    out_resolved->native_kind = to_c_native_kind(resolved->native.kind);
    out_resolved->native_value = resolved->native.value;
    out_resolved->channel_order = static_cast<uint32_t>(resolved->cpu_layout.order);
    out_resolved->component_type = static_cast<uint32_t>(resolved->cpu_layout.component);
    out_resolved->component_bits = resolved->cpu_layout.component_bits;
    out_resolved->channel_count = resolved->cpu_layout.channel_count;
    out_resolved->bytes_per_pixel = resolved->cpu_layout.bytes_per_pixel;
    return NOZZLE_OK;
}

NozzleErrorCode nozzle_frame_lock_pixels_with_origin(
    NozzleFrame *frame,
    NozzleTextureOrigin desired_origin,
    NozzleMappedPixels *out_pixels
) {
    if (!frame || !out_pixels || !frame->obj) return NOZZLE_ERROR_INVALID_ARGUMENT;

    auto result = nozzle::lock_frame_pixels_with_origin(*frame->obj, to_cpp_origin(desired_origin));
    if (!result.ok()) return to_c_error(result.error().code);

    fill_c_mapped_pixels(result.value(), out_pixels);
    return NOZZLE_OK;
}

NozzleErrorCode nozzle_frame_lock_pixels_mapping_with_origin(
    NozzleFrame *frame,
    NozzleTextureOrigin desired_origin,
    NozzlePixelMapping **out_mapping,
    NozzleMappedPixels *out_pixels
) {
    if (out_mapping) {
        *out_mapping = nullptr;
    }
    if (out_pixels) {
        std::memset(out_pixels, 0, sizeof(NozzleMappedPixels));
    }
    if (!frame || !out_mapping || !out_pixels || !frame->obj) {
        return NOZZLE_ERROR_INVALID_ARGUMENT;
    }

    auto result = nozzle::lock_frame_pixels_mapping_with_origin(
        *frame->obj, to_cpp_origin(desired_origin));
    if (!result.ok()) return to_c_error(result.error().code);

    auto *wrapper = new (std::nothrow) NozzlePixelMapping{};
    if (!wrapper) {
        result.value().unlock();
        return NOZZLE_ERROR_RESOURCE_CREATION_FAILED;
    }

    NozzleErrorCode object_error = reset_unique_nothrow(
        wrapper->obj, std::move(result.value()));
    if (object_error != NOZZLE_OK) {
        delete wrapper;
        return object_error;
    }

    fill_c_mapped_pixels(wrapper->obj->pixels(), out_pixels);
    *out_mapping = wrapper;
    return NOZZLE_OK;
}

NozzleErrorCode nozzle_frame_copy_pixels_with_origin(
    NozzleFrame *frame,
    NozzleTextureOrigin desired_origin,
    void *out_data,
    uint64_t out_data_size_bytes,
    NozzleMappedPixels *out_pixels
) {
    if (!frame || !out_data || !out_pixels || !frame->obj) {
        return NOZZLE_ERROR_INVALID_ARGUMENT;
    }
    std::memset(out_pixels, 0, sizeof(NozzleMappedPixels));

    auto result = nozzle::lock_frame_pixels_mapping_with_origin(
        *frame->obj, to_cpp_origin(desired_origin));
    if (!result.ok()) return to_c_error(result.error().code);

    NozzleErrorCode copy_result = copy_mapped_pixels_to_buffer(
        result.value().pixels(), out_data, out_data_size_bytes, out_pixels);

    auto unlock_result = result.value().unlock_checked();
    if (copy_result == NOZZLE_OK && !unlock_result.ok()) {
        copy_result = to_c_error(unlock_result.error().code);
    }
    if (copy_result != NOZZLE_OK) {
        std::memset(out_pixels, 0, sizeof(NozzleMappedPixels));
    }
    return copy_result;
}

void nozzle_frame_unlock_pixels(NozzleFrame *frame) {
    if (!frame || !frame->obj) return;
    nozzle::unlock_frame_pixels(*frame->obj);
}

NozzleErrorCode nozzle_frame_lock_writable_pixels_with_origin(
    NozzleFrame *frame,
    NozzleTextureOrigin desired_origin,
    NozzleMappedPixels *out_pixels
) {
    if (!frame || !out_pixels || !frame->writable) return NOZZLE_ERROR_INVALID_ARGUMENT;

    auto result = nozzle::lock_writable_pixels_with_origin(*frame->writable, to_cpp_origin(desired_origin));
    if (!result.ok()) return to_c_error(result.error().code);

    fill_c_mapped_pixels(result.value(), out_pixels);
    return NOZZLE_OK;
}

NozzleErrorCode nozzle_frame_lock_writable_pixels_mapping_with_origin(
    NozzleFrame *frame,
    NozzleTextureOrigin desired_origin,
    NozzlePixelMapping **out_mapping,
    NozzleMappedPixels *out_pixels
) {
    if (out_mapping) {
        *out_mapping = nullptr;
    }
    if (out_pixels) {
        std::memset(out_pixels, 0, sizeof(NozzleMappedPixels));
    }
    if (!frame || !out_mapping || !out_pixels || !frame->writable) {
        return NOZZLE_ERROR_INVALID_ARGUMENT;
    }

    auto result = nozzle::lock_writable_pixels_mapping_with_origin(
        *frame->writable, to_cpp_origin(desired_origin));
    if (!result.ok()) return to_c_error(result.error().code);

    auto *wrapper = new (std::nothrow) NozzlePixelMapping{};
    if (!wrapper) {
        result.value().unlock();
        return NOZZLE_ERROR_RESOURCE_CREATION_FAILED;
    }

    NozzleErrorCode object_error = reset_unique_nothrow(
        wrapper->obj, std::move(result.value()));
    if (object_error != NOZZLE_OK) {
        delete wrapper;
        return object_error;
    }

    fill_c_mapped_pixels(wrapper->obj->pixels(), out_pixels);
    *out_mapping = wrapper;
    return NOZZLE_OK;
}

NozzleErrorCode nozzle_pixel_mapping_unlock_checked(NozzlePixelMapping **mapping) {
    if (!mapping || !*mapping || !(*mapping)->obj) {
        return NOZZLE_ERROR_INVALID_ARGUMENT;
    }

    NozzlePixelMapping *wrapper = *mapping;
    auto result = wrapper->obj->unlock_checked();
    delete wrapper;
    *mapping = nullptr;
    if (!result.ok()) return to_c_error(result.error().code);
    return NOZZLE_OK;
}

void nozzle_pixel_mapping_unlock(NozzlePixelMapping **mapping) {
    if (!mapping || !*mapping) {
        return;
    }
    (void)nozzle_pixel_mapping_unlock_checked(mapping);
}

NozzleErrorCode nozzle_frame_unlock_writable_pixels_checked(NozzleFrame *frame) {
    if (!frame || !frame->writable) return NOZZLE_ERROR_INVALID_ARGUMENT;
    auto result = nozzle::unlock_writable_pixels_checked(*frame->writable);
    if (!result.ok()) return to_c_error(result.error().code);
    return NOZZLE_OK;
}

void nozzle_frame_unlock_writable_pixels(NozzleFrame *frame) {
    (void)nozzle_frame_unlock_writable_pixels_checked(frame);
}

#if NOZZLE_ENABLE_TEST_HOOKS
void nozzle_test_mark_writable_frame_cpu_unlock_failed(NozzleFrame *frame) {
    if (!frame || !frame->writable) return;
    nozzle::detail::mark_writable_frame_cpu_unlock_failed(*frame->writable);
}

void nozzle_test_fail_next_writable_frame_wrapper_alloc(void) {
    g_fail_next_writable_frame_wrapper_alloc = true;
}

void nozzle_test_fail_next_c_api_wrapper_object_alloc(void) {
    g_fail_next_c_api_wrapper_object_alloc = true;
}

void nozzle_test_clear_c_api_wrapper_object_alloc_failure(void) {
    g_fail_next_c_api_wrapper_object_alloc = false;
}

NozzleErrorCode nozzle_test_copy_mapped_pixels_to_buffer(
    const void *source_data,
    int64_t source_row_stride_bytes,
    uint32_t width,
    uint32_t height,
    NozzleTextureFormat format,
    NozzleTextureOrigin origin,
    uint8_t bytes_per_pixel,
    void *out_data,
    uint64_t out_data_size_bytes,
    NozzleMappedPixels *out_pixels
) {
    nozzle::mapped_pixels mapped{};
    mapped.data = const_cast<void *>(source_data);
    mapped.row_stride_bytes = source_row_stride_bytes;
    mapped.width = width;
    mapped.height = height;
    mapped.format = to_cpp_format(format);
    mapped.origin = to_cpp_origin(origin);
    mapped.cpu_layout.bytes_per_pixel = bytes_per_pixel;
    return copy_mapped_pixels_to_buffer(
        mapped, out_data, out_data_size_bytes, out_pixels);
}
#endif

// ========== GL Interop ==========

NozzleErrorCode nozzle_sender_publish_gl_texture(
    NozzleSender *sender,
    uint32_t gl_texture_name,
    uint32_t gl_target,
    uint32_t width,
    uint32_t height,
    NozzleTextureFormat format
) {
#if NOZZLE_HAS_OPENGL
    if (!sender) return NOZZLE_ERROR_INVALID_ARGUMENT;

    nozzle::gl::gl_texture_desc gl_desc{};
    gl_desc.name = gl_texture_name;
    gl_desc.target = gl_target;
    gl_desc.width = width;
    gl_desc.height = height;
    gl_desc.format = to_cpp_format(format);
    gl_desc.origin = nozzle::texture_origin::bottom_left;

    auto result = nozzle::gl::publish_gl_texture(*sender->obj, gl_desc);
    if (!result.ok()) return to_c_error(result.error().code);
    return NOZZLE_OK;
#else
    return NOZZLE_ERROR_UNSUPPORTED_BACKEND;
#endif
}

NozzleErrorCode nozzle_frame_copy_to_gl_texture(
    NozzleFrame *frame,
    uint32_t gl_texture_name,
    uint32_t gl_target,
    uint32_t width,
    uint32_t height,
    NozzleTextureFormat format
) {
#if NOZZLE_HAS_OPENGL
    if (!frame || !frame->obj) return NOZZLE_ERROR_INVALID_ARGUMENT;

    nozzle::gl::gl_texture_desc gl_desc{};
    gl_desc.name = gl_texture_name;
    gl_desc.target = gl_target;
    gl_desc.width = width;
    gl_desc.height = height;
    gl_desc.format = to_cpp_format(format);
    gl_desc.origin = nozzle::texture_origin::bottom_left;

    auto result = nozzle::gl::copy_frame_to_gl_texture(*frame->obj, gl_desc);
    if (!result.ok()) return to_c_error(result.error().code);
    return NOZZLE_OK;
#else
    return NOZZLE_ERROR_UNSUPPORTED_BACKEND;
#endif
}

// ========== Native Texture Interop (GPU) ==========

NozzleErrorCode nozzle_sender_publish_native_texture(
    NozzleSender *sender,
    void *native_texture,
    uint32_t width,
    uint32_t height,
    NozzleTextureFormat format
) {
    if (!sender || !native_texture) return NOZZLE_ERROR_INVALID_ARGUMENT;

    auto cpp_fmt = to_cpp_format(format);
    auto result = sender->obj->publish_native_texture(
        native_texture, width, height, cpp_fmt, cpp_fmt);
    if (!result.ok()) return to_c_error(result.error().code);
    return NOZZLE_OK;
}

NozzleErrorCode nozzle_sender_publish_native_texture_ex(
    NozzleSender *sender,
    void *native_texture,
    uint32_t width,
    uint32_t height,
    NozzleTextureFormat storage_format,
    NozzleTextureFormat semantic_format
) {
    if (!sender || !native_texture) return NOZZLE_ERROR_INVALID_ARGUMENT;

    auto result = sender->obj->publish_native_texture(
        native_texture, width, height, to_cpp_format(storage_format), to_cpp_format(semantic_format));
    if (!result.ok()) return to_c_error(result.error().code);
    return NOZZLE_OK;
}

NozzleErrorCode nozzle_sender_publish_dmabuf(
    NozzleSender *sender,
    const NozzleDmaBufPublishDesc *desc
) {
    NozzleErrorCode validation = validate_dmabuf_publish_desc(desc);
    if (validation != NOZZLE_OK) return validation;
    if (!sender || !sender->obj) return NOZZLE_ERROR_INVALID_ARGUMENT;

#if NOZZLE_HAS_DMA_BUF
    nozzle::dma_buf::publish_desc cpp_desc{};
    cpp_desc.dmabuf_fd = desc->dmabuf_fd;
    cpp_desc.width = desc->width;
    cpp_desc.height = desc->height;
    cpp_desc.fourcc = desc->drm_fourcc;
    cpp_desc.modifier = desc->modifier;
    cpp_desc.plane_count = desc->plane_count;
    for (uint32_t i = 0; i < desc->plane_count && i < 4; ++i) {
        cpp_desc.planes[i].stride = desc->planes[i].stride;
        cpp_desc.planes[i].offset = desc->planes[i].offset;
    }
    cpp_desc.storage_format = to_cpp_format(desc->storage_format);
    cpp_desc.semantic_format = to_cpp_format(desc->semantic_format);
    cpp_desc.swizzle = to_cpp_swizzle(desc->swizzle);

    auto result = sender->obj->publish_dmabuf_texture(cpp_desc);
    if (!result.ok()) return to_c_error(result.error().code);
    return NOZZLE_OK;
#else
    return NOZZLE_ERROR_UNSUPPORTED_BACKEND;
#endif
}

NozzleErrorCode nozzle_frame_copy_to_native_texture(
    NozzleFrame *frame,
    void *native_texture,
    uint32_t width,
    uint32_t height,
    NozzleTextureFormat format
) {
    if (!frame || !frame->obj || !native_texture) return NOZZLE_ERROR_INVALID_ARGUMENT;

    auto result = frame->obj->copy_to_native_texture(
        native_texture, width, height, to_cpp_format(format));
    if (!result.ok()) return to_c_error(result.error().code);
    return NOZZLE_OK;
}

// ========== Texture Wrap ==========

NozzleErrorCode nozzle_texture_wrap(
    const NozzleTextureWrapDesc *desc,
    NozzleTexture **out_texture
) {
    if (!desc || !out_texture || !desc->native_texture) return NOZZLE_ERROR_INVALID_ARGUMENT;
    *out_texture = nullptr;

#if NOZZLE_HAS_METAL
    if (desc->backend == NOZZLE_BACKEND_METAL) {
        void *surface = nozzle::metal::get_io_surface_from_native_texture(desc->native_texture);
        nozzle::metal::texture_wrap_desc wrap_desc{};
        wrap_desc.texture = desc->native_texture;
        wrap_desc.io_surface = surface;
        wrap_desc.width = desc->width;
        wrap_desc.height = desc->height;
        wrap_desc.format = static_cast<uint32_t>(to_cpp_format(desc->format));

        auto result = nozzle::metal::wrap_texture(wrap_desc);
        if (!result.ok()) return to_c_error(result.error().code);

        auto *wrapper = new (std::nothrow) NozzleTexture{};
        if (!wrapper) return NOZZLE_ERROR_RESOURCE_CREATION_FAILED;

        NozzleErrorCode object_error = reset_unique_nothrow(
            wrapper->obj, std::move(result.value()));
        if (object_error != NOZZLE_OK) {
            delete wrapper;
            return object_error;
        }

        *out_texture = wrapper;
        return NOZZLE_OK;
    }
#endif

    return NOZZLE_ERROR_UNSUPPORTED_BACKEND;
}

void nozzle_texture_destroy(NozzleTexture *texture) {
    delete texture;
}

NozzleErrorCode nozzle_swizzle_channels(
    const void *src,
    void *dst,
    uint32_t width,
    uint32_t height,
    int64_t src_row_bytes,
    int64_t dst_row_bytes,
    NozzleTextureFormat format,
    const uint8_t permute_map[4]
) {
    if (!permute_map) {
        return NOZZLE_ERROR_INVALID_ARGUMENT;
    }
    for (int i = 0; i < 4; ++i) {
        if (permute_map[i] > 3) {
            return NOZZLE_ERROR_INVALID_ARGUMENT;
        }
    }

    nozzle::channel_permute permute{};
    std::memcpy(permute.map, permute_map, 4);

    auto result = nozzle::swizzle_channels(
        src, dst,
        width, height,
        src_row_bytes, dst_row_bytes,
        to_cpp_format(format),
        permute);

    return to_c_error(result.ok() ? nozzle::ErrorCode::Ok : result.error().code);
}

NozzleErrorCode nozzle_widen_uint16_to_uint32(
    const void *src,
    void *dst,
    uint32_t width,
    uint32_t height,
    int64_t src_row_bytes,
    int64_t dst_row_bytes,
    uint32_t channels
) {
    auto result = nozzle::widen_uint16_to_uint32(
        src, dst, width, height, src_row_bytes, dst_row_bytes, channels);
    return to_c_error(result.ok() ? nozzle::ErrorCode::Ok : result.error().code);
}

NozzleErrorCode nozzle_convert_uint32_to_float32(
    const void *src,
    void *dst,
    uint32_t width,
    uint32_t height,
    int64_t src_row_bytes,
    int64_t dst_row_bytes,
    uint32_t channels
) {
    auto result = nozzle::convert_uint32_to_float32(
        src, dst, width, height, src_row_bytes, dst_row_bytes, channels);
    return to_c_error(result.ok() ? nozzle::ErrorCode::Ok : result.error().code);
}

NozzleErrorCode nozzle_widen_half_to_float(
    const void *src,
    void *dst,
    uint32_t width,
    uint32_t height,
    int64_t src_row_bytes,
    int64_t dst_row_bytes,
    uint32_t channels
) {
    auto result = nozzle::widen_half_to_float(
        src, dst, width, height, src_row_bytes, dst_row_bytes, channels);
    return to_c_error(result.ok() ? nozzle::ErrorCode::Ok : result.error().code);
}

NozzleErrorCode nozzle_fill_opaque_alpha_channel(
    void *data,
    uint32_t width,
    uint32_t height,
    int64_t row_stride_bytes,
    NozzleTextureFormat storage_format
) {
    auto fmt = to_cpp_format(storage_format);
    auto result = nozzle::fill_opaque_alpha_channel(
        data, width, height, static_cast<std::ptrdiff_t>(row_stride_bytes), fmt);
    return to_c_error(result.ok() ? nozzle::ErrorCode::Ok : result.error().code);
}

} // extern "C"
