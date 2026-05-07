// nozzle - linux_texture.cpp - DMA-BUF texture management

#include "linux_helpers.hpp"
#include "linux_fd_transfer.hpp"

#include <nozzle/backends/linux.hpp>
#include <nozzle/result.hpp>
#include <nozzle/types.hpp>
#include <nozzle/texture.hpp>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <gbm.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <array>
#include <cstring>
#include <drm_fourcc.h>
#include <string>
#include <xf86drm.h>

namespace nozzle::detail::linux_backend {

namespace {

void *g_egl_display{nullptr};
void *g_gbm_device{nullptr};
int g_drm_fd{-1};

#ifndef fourcc_code
#define fourcc_code(a, b, c, d) ((uint32_t)(a) | ((uint32_t)(b) << 8) | \
    ((uint32_t)(c) << 16) | ((uint32_t)(d) << 24))
#endif

#ifndef DRM_FORMAT_MOD_LINEAR
#define DRM_FORMAT_MOD_LINEAR 0
#endif

#ifndef DRM_FORMAT_R32
#define DRM_FORMAT_R32 fourcc_code('R', '3', '2', ' ')
#endif

#ifndef DRM_FORMAT_RG3232
#define DRM_FORMAT_RG3232 fourcc_code('R', 'G', '3', '2')
#endif

#ifndef DRM_FORMAT_RGBA16161616
#define DRM_FORMAT_RGBA16161616 fourcc_code('R', 'A', '6', '4')
#endif

#ifndef DRM_FORMAT_RGBA32323232
#define DRM_FORMAT_RGBA32323232 fourcc_code('R', 'A', '1', '2')
#endif

struct egl_ext_procs {
    PFNEGLCREATEIMAGEKHRPROC create_image{nullptr};
    PFNEGLDESTROYIMAGEKHRPROC destroy_image{nullptr};
};

egl_ext_procs g_egl_ext{};

bool init_egl_extensions() {
    if (g_egl_ext.create_image && g_egl_ext.destroy_image) {
        return true;
    }

    g_egl_ext.create_image = reinterpret_cast<PFNEGLCREATEIMAGEKHRPROC>(
        eglGetProcAddress("eglCreateImageKHR"));
    g_egl_ext.destroy_image = reinterpret_cast<PFNEGLDESTROYIMAGEKHRPROC>(
        eglGetProcAddress("eglDestroyImageKHR"));

    return g_egl_ext.create_image && g_egl_ext.destroy_image;
}

} // anonymous namespace

int open_drm_render_node() {
    int fd = open("/dev/dri/renderD128", O_RDWR);
    if (fd < 0) {
        return -1;
    }
    return fd;
}

void *create_gbm_device(int drm_fd) {
    if (drm_fd < 0) {
        return nullptr;
    }
    auto *dev = gbm_create_device(drm_fd);
    return static_cast<void *>(dev);
}

void *get_egl_display() {
    if (g_egl_display) {
        return g_egl_display;
    }

    if (!g_gbm_device) {
        int fd = open_drm_render_node();
        if (fd < 0) {
            return nullptr;
        }
        g_drm_fd = fd;
        g_gbm_device = create_gbm_device(fd);
        if (!g_gbm_device) {
            close(fd);
            g_drm_fd = -1;
            return nullptr;
        }
    }

    g_egl_display = eglGetDisplay(
        reinterpret_cast<EGLNativeDisplayType>(g_gbm_device));
    if (g_egl_display == EGL_NO_DISPLAY) {
        return nullptr;
    }

    EGLint major{0}, minor{0};
    if (!eglInitialize(g_egl_display, &major, &minor)) {
        g_egl_display = nullptr;
        return nullptr;
    }

    if (!init_egl_extensions()) {
        eglTerminate(g_egl_display);
        g_egl_display = nullptr;
        return nullptr;
    }

    return g_egl_display;
}

void *get_default_gbm_device() {
    if (g_gbm_device) {
        return g_gbm_device;
    }

    if (!get_egl_display()) {
        return nullptr;
    }

    return g_gbm_device;
}

void release_linux_device(void * /*device*/) {
}

uint32_t drm_format_from_nozzle(uint32_t nozzle_format) {
    switch (static_cast<texture_format>(nozzle_format)) {
        case texture_format::r8_unorm:         return DRM_FORMAT_R8;
        case texture_format::rg8_unorm:        return DRM_FORMAT_RG88;
        case texture_format::rgb8_unorm:       return DRM_FORMAT_RGB888;
        case texture_format::rgba8_unorm:      return DRM_FORMAT_RGBA8888;
        case texture_format::bgra8_unorm:      return DRM_FORMAT_BGRA8888;
        case texture_format::rgba8_srgb:       return DRM_FORMAT_RGBA8888;
        case texture_format::bgra8_srgb:       return DRM_FORMAT_BGRA8888;
        case texture_format::r16_unorm:        return DRM_FORMAT_R16;
        case texture_format::rg16_unorm:       return DRM_FORMAT_RG1616;
        case texture_format::rgb16_unorm:      return DRM_FORMAT_RGBA16161616;
        case texture_format::rgba16_unorm:     return DRM_FORMAT_RGBA16161616;
        case texture_format::r16_float:        return DRM_FORMAT_R16;
        case texture_format::rg16_float:       return DRM_FORMAT_RG1616;
        case texture_format::rgb16_float:      return DRM_FORMAT_RGBA16161616;
        case texture_format::rgba16_float:     return DRM_FORMAT_RGBA16161616;
        case texture_format::r32_float:        return DRM_FORMAT_R32;
        case texture_format::rg32_float:       return DRM_FORMAT_RG3232;
        case texture_format::rgb32_float:      return DRM_FORMAT_RGBA32323232;
        case texture_format::rgba32_float:     return DRM_FORMAT_RGBA32323232;
        case texture_format::r32_uint:         return DRM_FORMAT_R32;
        case texture_format::rgba32_uint:      return DRM_FORMAT_RGBA32323232;
        case texture_format::rgb32_uint:       return DRM_FORMAT_RGBA32323232;
        default:                               return DRM_FORMAT_INVALID;
    }
}

Result<dmabuf_allocation> allocate_dmabuf(
    void *gbm_dev,
    uint32_t width,
    uint32_t height,
    uint32_t format
) {
    auto *dev = static_cast<struct gbm_device *>(gbm_dev);
    if (!dev) {
        return Error{ErrorCode::InvalidArgument, "GBM device is null"};
    }
    if (width == 0 || height == 0) {
        return Error{ErrorCode::InvalidArgument, "Texture dimensions must be non-zero"};
    }

    uint32_t fourcc = drm_format_from_nozzle(format);
    if (fourcc == DRM_FORMAT_INVALID) {
        return Error{ErrorCode::UnsupportedFormat,
            "Unsupported nozzle pixel format for DMA-BUF allocation"};
    }

    uint32_t flags = GBM_BO_USE_RENDERING | GBM_BO_USE_LINEAR;
    if (fourcc == DRM_FORMAT_RGBA16161616 || fourcc == DRM_FORMAT_RGBA32323232) {
        flags |= GBM_BO_USE_RENDERING;
    }

    uint64_t linear_mod = DRM_FORMAT_MOD_LINEAR;
    auto *bo = gbm_bo_create_with_modifiers(dev, width, height, fourcc,
        &linear_mod, 1);
    if (!bo) {
        bo = gbm_bo_create(dev, width, height, fourcc, flags);
    }
    if (!bo) {
        return Error{ErrorCode::ResourceCreationFailed,
            "Failed to create GBM buffer object"};
    }

    int fd = gbm_bo_get_fd(bo);
    if (fd < 0) {
        gbm_bo_destroy(bo);
        return Error{ErrorCode::ResourceCreationFailed,
            "Failed to export DMA-BUF fd from GBM buffer object"};
    }

    dmabuf_allocation alloc{};
    alloc.fd = fd;
    alloc.plane_count = gbm_bo_get_plane_count(bo);
    for (uint32_t i = 0; i < alloc.plane_count && i < 4; ++i) {
        alloc.planes[i].stride = gbm_bo_get_stride_for_plane(bo, i);
        alloc.planes[i].offset = gbm_bo_get_offset(bo, i);
    }
    uint64_t mod = gbm_bo_get_modifier(bo);
    if (mod == ((1ULL << 56) - 1)) {
        mod = DRM_FORMAT_MOD_LINEAR;
    }
    alloc.modifier = mod;
    alloc.gbm_bo = static_cast<void *>(bo);

    return alloc;
}

void *import_egl_image(
    void *egl_display,
    int fd,
    uint32_t width,
    uint32_t height,
    uint32_t fourcc,
    uint32_t plane_count,
    const dmabuf_plane *planes,
    uint64_t modifier
) {
    if (!init_egl_extensions()) {
        return nullptr;
    }

    auto *display = static_cast<EGLDisplay>(egl_display);
    if (display == EGL_NO_DISPLAY || fd < 0) {
        return nullptr;
    }

    if (!planes || plane_count < 1 || plane_count > 4) {
        return nullptr;
    }

    EGLint attribs[64]{};
    int idx = 0;
    attribs[idx++] = EGL_WIDTH;
    attribs[idx++] = static_cast<EGLint>(width);
    attribs[idx++] = EGL_HEIGHT;
    attribs[idx++] = static_cast<EGLint>(height);
    attribs[idx++] = EGL_LINUX_DRM_FOURCC_EXT;
    attribs[idx++] = static_cast<EGLint>(fourcc);

    attribs[idx++] = EGL_DMA_BUF_PLANE0_FD_EXT;
    attribs[idx++] = static_cast<EGLint>(fd);
    attribs[idx++] = EGL_DMA_BUF_PLANE0_PITCH_EXT;
    attribs[idx++] = static_cast<EGLint>(planes[0].stride);
    attribs[idx++] = EGL_DMA_BUF_PLANE0_OFFSET_EXT;
    attribs[idx++] = static_cast<EGLint>(planes[0].offset);

    if (plane_count > 1) {
        attribs[idx++] = EGL_DMA_BUF_PLANE1_FD_EXT;
        attribs[idx++] = static_cast<EGLint>(fd);
        attribs[idx++] = EGL_DMA_BUF_PLANE1_PITCH_EXT;
        attribs[idx++] = static_cast<EGLint>(planes[1].stride);
        attribs[idx++] = EGL_DMA_BUF_PLANE1_OFFSET_EXT;
        attribs[idx++] = static_cast<EGLint>(planes[1].offset);
    }
    if (plane_count > 2) {
        attribs[idx++] = EGL_DMA_BUF_PLANE2_FD_EXT;
        attribs[idx++] = static_cast<EGLint>(fd);
        attribs[idx++] = EGL_DMA_BUF_PLANE2_PITCH_EXT;
        attribs[idx++] = static_cast<EGLint>(planes[2].stride);
        attribs[idx++] = EGL_DMA_BUF_PLANE2_OFFSET_EXT;
        attribs[idx++] = static_cast<EGLint>(planes[2].offset);
    }
    if (plane_count > 3) {
        attribs[idx++] = EGL_DMA_BUF_PLANE3_FD_EXT;
        attribs[idx++] = static_cast<EGLint>(fd);
        attribs[idx++] = EGL_DMA_BUF_PLANE3_PITCH_EXT;
        attribs[idx++] = static_cast<EGLint>(planes[3].stride);
        attribs[idx++] = EGL_DMA_BUF_PLANE3_OFFSET_EXT;
        attribs[idx++] = static_cast<EGLint>(planes[3].offset);
    }

    if (modifier != DRM_FORMAT_MOD_LINEAR && modifier != ((1ULL << 56) - 1)) {
        attribs[idx++] = EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT;
        attribs[idx++] = static_cast<EGLint>(modifier & 0xFFFFFFFF);
        attribs[idx++] = EGL_DMA_BUF_PLANE0_MODIFIER_HI_EXT;
        attribs[idx++] = static_cast<EGLint>(modifier >> 32);
        if (plane_count > 1) {
            attribs[idx++] = EGL_DMA_BUF_PLANE1_MODIFIER_LO_EXT;
            attribs[idx++] = static_cast<EGLint>(modifier & 0xFFFFFFFF);
            attribs[idx++] = EGL_DMA_BUF_PLANE1_MODIFIER_HI_EXT;
            attribs[idx++] = static_cast<EGLint>(modifier >> 32);
        }
        if (plane_count > 2) {
            attribs[idx++] = EGL_DMA_BUF_PLANE2_MODIFIER_LO_EXT;
            attribs[idx++] = static_cast<EGLint>(modifier & 0xFFFFFFFF);
            attribs[idx++] = EGL_DMA_BUF_PLANE2_MODIFIER_HI_EXT;
            attribs[idx++] = static_cast<EGLint>(modifier >> 32);
        }
        if (plane_count > 3) {
            attribs[idx++] = EGL_DMA_BUF_PLANE3_MODIFIER_LO_EXT;
            attribs[idx++] = static_cast<EGLint>(modifier & 0xFFFFFFFF);
            attribs[idx++] = EGL_DMA_BUF_PLANE3_MODIFIER_HI_EXT;
            attribs[idx++] = static_cast<EGLint>(modifier >> 32);
        }
    }

    attribs[idx++] = EGL_NONE;

    auto *image = g_egl_ext.create_image(
        display,
        EGL_NO_CONTEXT,
        EGL_LINUX_DMA_BUF_EXT,
        nullptr,
        attribs);

    return static_cast<void *>(image);
}

void destroy_egl_image(void *egl_display, void *egl_image) {
    if (!egl_image || !init_egl_extensions()) {
        return;
    }
    auto *display = static_cast<EGLDisplay>(egl_display);
    g_egl_ext.destroy_image(display, static_cast<EGLImage>(egl_image));
}

void destroy_gbm_bo(void *gbm_bo) {
    if (gbm_bo) {
        gbm_bo_destroy(static_cast<struct gbm_bo *>(gbm_bo));
    }
}

void close_drm_fd(int fd) {
    if (fd >= 0) {
        close(fd);
    }
}

void destroy_gbm_device(void *gbm_dev) {
    if (gbm_dev) {
        gbm_device_destroy(static_cast<struct gbm_device *>(gbm_dev));
    }
}

void destroy_dmabuf_allocation(dmabuf_allocation &alloc) {
    destroy_gbm_bo(alloc.gbm_bo);
    alloc.gbm_bo = nullptr;
    if (alloc.fd >= 0) {
        close(alloc.fd);
        alloc.fd = -1;
    }
    alloc.plane_count = 0;
    alloc.modifier = 0;
}

} // namespace nozzle::detail::linux_backend

namespace nozzle::detail::linux_backend {

Result<texture> create_dmabuf_texture(
    void *device,
    uint32_t width,
    uint32_t height,
    uint32_t format
) {
    void *gbm_dev = device ? device : get_default_gbm_device();
    if (!gbm_dev) {
        return Error{ErrorCode::BackendError, "No GBM device available"};
    }

    auto alloc_result = allocate_dmabuf(gbm_dev, width, height, format);
    if (!alloc_result.ok()) {
        return alloc_result.error();
    }
    auto alloc = std::move(alloc_result.value());

    void *egl_disp = get_egl_display();
    if (!egl_disp) {
        destroy_dmabuf_allocation(alloc);
        return Error{ErrorCode::BackendError, "No EGL display available"};
    }

    uint32_t fourcc = drm_format_from_nozzle(format);
    void *egl_image = import_egl_image(
        egl_disp, alloc.fd, width, height, fourcc, alloc.plane_count, alloc.planes, alloc.modifier);
    if (!egl_image) {
        destroy_dmabuf_allocation(alloc);
        return Error{ErrorCode::ResourceCreationFailed,
            "Failed to import DMA-BUF fd as EGLImage"};
    }

    void *native_texture = egl_image;
    void *native_surface = reinterpret_cast<void *>(static_cast<intptr_t>(alloc.fd));

    // GBM BO can be destroyed after fd export and EGLImage import.
    // The prime fd is independent of the BO.
    destroy_gbm_bo(alloc.gbm_bo);
    alloc.gbm_bo = nullptr;
    alloc.fd = -1;

    native_format_desc native{};
    native.backend = backend_type::dma_buf;
    native.kind = native_format_kind::drm_fourcc;
    native.value = fourcc;
    native.modifier = alloc.modifier;
    native.plane_count = alloc.plane_count;
    for (uint32_t i = 0; i < alloc.plane_count && i < 4; ++i) {
        native.plane_strides[i] = alloc.planes[i].stride;
        native.plane_offsets[i] = alloc.planes[i].offset;
    }

    return make_texture_from_backend(native_texture, native_surface, width, height, format, 0, &native);
}

Result<texture> import_dmabuf_texture(
    int fd,
    uint32_t width,
    uint32_t height,
    uint32_t fourcc,
    uint32_t plane_count,
    const dmabuf_plane *planes,
    uint64_t modifier
) {
    if (fd < 0) {
        return Error{ErrorCode::InvalidArgument, "DMA-BUF fd is invalid"};
    }

    void *egl_disp = get_egl_display();
    if (!egl_disp) {
        return Error{ErrorCode::BackendError, "No EGL display available"};
    }

    void *egl_image = import_egl_image(
        egl_disp, fd, width, height, fourcc, plane_count, planes, modifier);
    if (!egl_image) {
        return Error{ErrorCode::ResourceCreationFailed,
            "Failed to import DMA-BUF fd as EGLImage"};
    }

    void *native_texture = egl_image;
    void *native_surface = reinterpret_cast<void *>(static_cast<intptr_t>(fd));

    native_format_desc native{};
    native.backend = backend_type::dma_buf;
    native.kind = native_format_kind::drm_fourcc;
    native.value = fourcc;
    native.modifier = modifier;
    native.plane_count = plane_count;
    for (uint32_t i = 0; i < plane_count && i < 4; ++i) {
        native.plane_strides[i] = planes[i].stride;
        native.plane_offsets[i] = planes[i].offset;
    }

    return make_texture_from_backend(native_texture, native_surface, width, height, 0, 0, &native);
}

} // namespace nozzle::detail::linux_backend

namespace nozzle::detail::linux_backend {

dmabuf_texture_cache::~dmabuf_texture_cache() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (size_t i = 0; i < entries_.size(); ++i) {
        if (valid_[i] && entries_[i].fd >= 0) {
            close(entries_[i].fd);
        }
    }
}

void dmabuf_texture_cache::store(
    uint32_t slot_index, int fd, uint32_t width, uint32_t height, uint32_t format,
    uint32_t plane_count, const uint32_t *plane_strides, const uint32_t *plane_offsets
) {
    if (slot_index >= 8) {
        return;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (valid_[slot_index] && entries_[slot_index].fd >= 0) {
        close(entries_[slot_index].fd);
    }
    entries_[slot_index] = cache_entry{fd, width, height, format, plane_count, {}, {}};
    if (plane_strides && plane_offsets) {
        for (uint32_t i = 0; i < plane_count && i < 4; ++i) {
            entries_[slot_index].plane_strides[i] = plane_strides[i];
            entries_[slot_index].plane_offsets[i] = plane_offsets[i];
        }
    }
    valid_[slot_index] = true;
}

bool dmabuf_texture_cache::has(uint32_t slot_index) const {
    if (slot_index >= 8) {
        return false;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    return valid_[slot_index] && entries_[slot_index].fd >= 0;
}

int dmabuf_texture_cache::get_fd(uint32_t slot_index) const {
    if (slot_index >= 8) {
        return -1;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (!valid_[slot_index]) {
        return -1;
    }
    return entries_[slot_index].fd;
}

uint32_t dmabuf_texture_cache::get_plane_count(uint32_t slot_index) const {
    if (slot_index >= 8) {
        return 0;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (!valid_[slot_index]) {
        return 0;
    }
    return entries_[slot_index].plane_count;
}

void dmabuf_texture_cache::get_plane_metadata(
    uint32_t slot_index, uint32_t &plane_count, uint32_t (&strides)[4], uint32_t (&offsets)[4]
) const {
    plane_count = 0;
    for (uint32_t i = 0; i < 4; ++i) {
        strides[i] = 0;
        offsets[i] = 0;
    }
    if (slot_index >= 8) {
        return;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (!valid_[slot_index]) {
        return;
    }
    plane_count = entries_[slot_index].plane_count;
    for (uint32_t i = 0; i < plane_count && i < 4; ++i) {
        strides[i] = entries_[slot_index].plane_strides[i];
        offsets[i] = entries_[slot_index].plane_offsets[i];
    }
}

} // namespace nozzle::detail::linux_backend

namespace nozzle::dma_buf {

Result<device> wrap_device(const device_desc &desc) {
    if (!desc.gbm_device) {
        return Error{ErrorCode::InvalidArgument, "GBM device is null"};
    }
    return detail::make_device_from_backend(desc.gbm_device);
}

Result<texture> wrap_texture(const texture_wrap_desc &desc) {
    if (!desc.egl_image) {
        return Error{ErrorCode::InvalidArgument, "EGLImage is null"};
    }
    if (desc.dmabuf_fd < 0) {
        return Error{ErrorCode::InvalidArgument, "DMA-BUF fd is invalid"};
    }
    if (desc.width == 0 || desc.height == 0) {
        return Error{ErrorCode::InvalidArgument, "Texture dimensions must be non-zero"};
    }

    void *native_texture = desc.egl_image;
    void *native_surface = reinterpret_cast<void *>(
        static_cast<intptr_t>(desc.dmabuf_fd));

    native_format_desc native{};
    native.backend = backend_type::dma_buf;
    native.kind = native_format_kind::drm_fourcc;
    native.value = desc.fourcc;
    native.modifier = desc.modifier;
    native.plane_count = desc.plane_count;
    for (uint32_t i = 0; i < desc.plane_count && i < 4; ++i) {
        native.plane_strides[i] = desc.plane_strides[i];
        native.plane_offsets[i] = desc.plane_offsets[i];
    }

    return detail::make_texture_from_backend(
        native_texture, native_surface, desc.width, desc.height, 0, 0, &native);
}

void *get_egl_image(const texture &tex) {
    return detail::get_texture_native(tex);
}

int get_dmabuf_fd(const texture &tex) {
    void *surface = detail::get_surface_native(tex);
    if (!surface) { return -1; }
    return static_cast<int>(reinterpret_cast<intptr_t>(surface));
}

} // namespace nozzle::dma_buf
