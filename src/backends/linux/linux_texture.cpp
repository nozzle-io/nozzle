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
        case texture_format::rgba8_unorm:      return DRM_FORMAT_RGBA8888;
        case texture_format::bgra8_unorm:      return DRM_FORMAT_BGRA8888;
        case texture_format::rgba8_srgb:       return DRM_FORMAT_RGBA8888;
        case texture_format::bgra8_srgb:       return DRM_FORMAT_BGRA8888;
        case texture_format::r16_unorm:        return DRM_FORMAT_R16;
        case texture_format::rg16_unorm:       return DRM_FORMAT_RG1616;
        case texture_format::rgba16_unorm:     return DRM_FORMAT_RGBA16161616;
        case texture_format::r16_float:        return DRM_FORMAT_R16;
        case texture_format::rg16_float:       return DRM_FORMAT_RG1616;
        case texture_format::rgba16_float:     return DRM_FORMAT_RGBA16161616;
        case texture_format::r32_float:        return DRM_FORMAT_R32;
        case texture_format::rg32_float:       return DRM_FORMAT_RG3232;
        case texture_format::rgba32_float:     return DRM_FORMAT_RGBA32323232;
        case texture_format::r32_uint:         return DRM_FORMAT_R32;
        case texture_format::rgba32_uint:      return DRM_FORMAT_RGBA32323232;
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

    uint32_t stride = gbm_bo_get_stride(bo);
    uint64_t modifier = DRM_FORMAT_MOD_LINEAR;

    dmabuf_allocation alloc{};
    alloc.fd = fd;
    alloc.stride = stride;
    alloc.modifier = modifier;
    alloc.gbm_bo = static_cast<void *>(bo);

    return alloc;
}

void *import_egl_image(
    void *egl_display,
    int fd,
    uint32_t width,
    uint32_t height,
    uint32_t fourcc,
    uint32_t stride,
    uint64_t modifier
) {
    if (!init_egl_extensions()) {
        return nullptr;
    }

    auto *display = static_cast<EGLDisplay>(egl_display);
    if (display == EGL_NO_DISPLAY || fd < 0) {
        return nullptr;
    }

    EGLint attribs[] = {
        EGL_WIDTH, static_cast<EGLint>(width),
        EGL_HEIGHT, static_cast<EGLint>(height),
        EGL_LINUX_DRM_FOURCC_EXT, static_cast<EGLint>(fourcc),
        EGL_DMA_BUF_PLANE0_FD_EXT, static_cast<EGLint>(fd),
        EGL_DMA_BUF_PLANE0_PITCH_EXT, static_cast<EGLint>(stride),
        EGL_DMA_BUF_PLANE0_OFFSET_EXT, static_cast<EGLint>(0),
    };

    EGLint modifier_attrs[] = {
        EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT,
            static_cast<EGLint>(modifier & 0xFFFFFFFF),
        EGL_DMA_BUF_PLANE0_MODIFIER_HI_EXT,
            static_cast<EGLint>(modifier >> 32),
        EGL_NONE,
    };

    EGLint final_attribs[32]{};
    std::memcpy(final_attribs, attribs, sizeof(attribs));

    if (modifier != DRM_FORMAT_MOD_LINEAR) {
        std::memcpy(
            final_attribs + (sizeof(attribs) / sizeof(EGLint)) - 1,
            modifier_attrs,
            sizeof(modifier_attrs));
    } else {
        size_t base_count = sizeof(attribs) / sizeof(EGLint);
        final_attribs[base_count - 1] = EGL_NONE;
    }

    auto *image = g_egl_ext.create_image(
        display,
        EGL_NO_CONTEXT,
        EGL_LINUX_DMA_BUF_EXT,
        nullptr,
        final_attribs);

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
    alloc.stride = 0;
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
        egl_disp, alloc.fd, width, height, fourcc, alloc.stride, alloc.modifier);
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

    return make_texture_from_backend(native_texture, native_surface, width, height, format);
}

Result<texture> import_dmabuf_texture(
    int fd,
    uint32_t width,
    uint32_t height,
    uint32_t fourcc,
    uint32_t stride,
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
        egl_disp, fd, width, height, fourcc, stride, modifier);
    if (!egl_image) {
        return Error{ErrorCode::ResourceCreationFailed,
            "Failed to import DMA-BUF fd as EGLImage"};
    }

    void *native_texture = egl_image;
    void *native_surface = reinterpret_cast<void *>(static_cast<intptr_t>(fd));

    return make_texture_from_backend(native_texture, native_surface, width, height, 0);
}

Result<texture> lookup_dmabuf_texture(
    uint32_t slot_index,
    uint32_t width,
    uint32_t height,
    uint32_t format
) {
    void *egl_disp = get_egl_display();
    if (!egl_disp) {
        return Error{ErrorCode::BackendError, "No EGL display available"};
    }

    uint32_t fourcc = drm_format_from_nozzle(format);
    if (fourcc == DRM_FORMAT_INVALID) {
        return Error{ErrorCode::UnsupportedFormat,
            "Unsupported nozzle pixel format for DMA-BUF lookup"};
    }

    return Error{ErrorCode::BackendError,
        "DMA-BUF lookup requires fd transfer — use lookup_dmabuf_texture_with_fds"};
}

Result<texture> lookup_dmabuf_texture_with_fds(
    const char *sender_uuid,
    uint32_t slot_index,
    uint32_t ring_size,
    uint32_t width,
    uint32_t height,
    uint32_t format,
    dmabuf_texture_cache &cache
) {
    if (!cache.has(slot_index)) {
        fd_receiver receiver;
        if (!receiver.connect(sender_uuid)) {
            return Error{ErrorCode::BackendError,
                "Failed to connect to sender fd socket"};
        }

        std::array<int, 8> fds{};
        fds.fill(-1);

        if (!receiver.recv_dmabuf_fds(fds.data(), ring_size)) {
            receiver.disconnect();
            return Error{ErrorCode::BackendError,
                "Failed to receive DMA-BUF fds from sender"};
        }
        receiver.disconnect();

        for (uint32_t i = 0; i < ring_size; ++i) {
            if (fds[i] >= 0) {
                cache.store(i, fds[i], width, height, format);
            }
        }
    }

    int fd = cache.get_fd(slot_index);
    if (fd < 0) {
        return Error{ErrorCode::ResourceCreationFailed,
            "No cached DMA-BUF fd for slot"};
    }

    uint32_t fourcc = drm_format_from_nozzle(format);
    void *egl_disp = get_egl_display();
    if (!egl_disp) {
        return Error{ErrorCode::BackendError, "No EGL display available"};
    }

    void *egl_image = import_egl_image(
        egl_disp, fd, width, height, fourcc, 0, DRM_FORMAT_MOD_LINEAR);
    if (!egl_image) {
        return Error{ErrorCode::ResourceCreationFailed,
            "Failed to import cached DMA-BUF fd as EGLImage"};
    }

    void *native_texture = egl_image;
    void *native_surface = reinterpret_cast<void *>(static_cast<intptr_t>(fd));

    return make_texture_from_backend(native_texture, native_surface, width, height, format);
}

} // namespace nozzle::detail::linux_backend

namespace nozzle::detail::linux_backend {

bool send_fds(int socket_fd, const int *fds, size_t count) {
    if (socket_fd < 0 || !fds || count == 0) {
        return false;
    }

    struct msghdr msg{};
    struct iovec iov{};
    char dummy = '\0';
    iov.iov_base = &dummy;
    iov.iov_len = 1;
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;

    size_t cmsg_size = CMSG_SPACE(sizeof(int) * count);
    auto *cmsg_buf = new char[cmsg_size];
    std::memset(cmsg_buf, 0, cmsg_size);

    msg.msg_control = cmsg_buf;
    msg.msg_controllen = cmsg_size;

    auto *cmsg = CMSG_FIRSTHDR(&msg);
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type = SCM_RIGHTS;
    cmsg->cmsg_len = CMSG_LEN(sizeof(int) * count);

    std::memcpy(CMSG_DATA(cmsg), fds, sizeof(int) * count);

    ssize_t sent = sendmsg(socket_fd, &msg, 0);
    delete[] cmsg_buf;

    return sent >= 0;
}

bool recv_fds(int socket_fd, int *fds, size_t count) {
    if (socket_fd < 0 || !fds || count == 0) {
        return false;
    }

    struct msghdr msg{};
    struct iovec iov{};
    char dummy{};
    iov.iov_base = &dummy;
    iov.iov_len = 1;
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;

    size_t cmsg_size = CMSG_SPACE(sizeof(int) * count);
    auto *cmsg_buf = new char[cmsg_size];
    std::memset(cmsg_buf, 0, cmsg_size);

    msg.msg_control = cmsg_buf;
    msg.msg_controllen = cmsg_size;

    ssize_t received = recvmsg(socket_fd, &msg, 0);
    if (received < 0) {
        delete[] cmsg_buf;
        return false;
    }

    for (size_t i = 0; i < count; ++i) {
        fds[i] = -1;
    }

    auto *cmsg = CMSG_FIRSTHDR(&msg);
    if (cmsg && cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SCM_RIGHTS) {
        size_t fd_count = (cmsg->cmsg_len - CMSG_LEN(0)) / sizeof(int);
        size_t copy_count = fd_count < count ? fd_count : count;
        std::memcpy(fds, CMSG_DATA(cmsg), sizeof(int) * copy_count);
    }

    delete[] cmsg_buf;
    return true;
}

fd_sender::fd_sender() = default;

fd_sender::~fd_sender() {
    stop();
}

fd_sender::fd_sender(fd_sender &&other) noexcept
    : listen_fd_{other.listen_fd_}
    , socket_path_{std::move(other.socket_path_)}
    , listening_{other.listening_}
{
    other.listen_fd_ = -1;
    other.listening_ = false;
}

fd_sender &fd_sender::operator=(fd_sender &&other) noexcept {
    if (this != &other) {
        stop();
        listen_fd_ = other.listen_fd_;
        socket_path_ = std::move(other.socket_path_);
        listening_ = other.listening_;
        other.listen_fd_ = -1;
        other.listening_ = false;
    }
    return *this;
}

bool fd_sender::start(const char *uuid) {
    if (listening_) {
        return true;
    }
    if (!uuid) {
        return false;
    }

    socket_path_ = "/tmp/nozzle_fd_";
    socket_path_ += uuid;

    listen_fd_ = socket(AF_UNIX, SOCK_STREAM, 0);
    if (listen_fd_ < 0) {
        return false;
    }

    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, socket_path_.c_str(), sizeof(addr.sun_path) - 1);

    unlink(socket_path_.c_str());

    if (bind(listen_fd_, reinterpret_cast<struct sockaddr *>(&addr),
             sizeof(addr)) < 0) {
        close(listen_fd_);
        listen_fd_ = -1;
        return false;
    }

    if (listen(listen_fd_, 1) < 0) {
        close(listen_fd_);
        listen_fd_ = -1;
        unlink(socket_path_.c_str());
        return false;
    }

    listening_ = true;
    return true;
}

void fd_sender::stop() {
    if (listen_fd_ >= 0) {
        close(listen_fd_);
        listen_fd_ = -1;
    }
    if (!socket_path_.empty()) {
        unlink(socket_path_.c_str());
        socket_path_.clear();
    }
    listening_ = false;
}

bool fd_sender::send_dmabuf_fds(const int *fds, size_t count) {
    if (!listening_ || listen_fd_ < 0) {
        return false;
    }

    struct sockaddr_un client_addr{};
    socklen_t client_len = sizeof(client_addr);
    int client_fd = accept(listen_fd_,
        reinterpret_cast<struct sockaddr *>(&client_addr), &client_len);
    if (client_fd < 0) {
        return false;
    }

    bool result = send_fds(client_fd, fds, count);
    close(client_fd);
    return result;
}

bool fd_sender::is_listening() const {
    return listening_;
}

fd_receiver::fd_receiver() = default;

fd_receiver::~fd_receiver() {
    disconnect();
}

fd_receiver::fd_receiver(fd_receiver &&other) noexcept
    : socket_fd_{other.socket_fd_}
    , socket_path_{std::move(other.socket_path_)}
    , connected_{other.connected_}
{
    other.socket_fd_ = -1;
    other.connected_ = false;
}

fd_receiver &fd_receiver::operator=(fd_receiver &&other) noexcept {
    if (this != &other) {
        disconnect();
        socket_fd_ = other.socket_fd_;
        socket_path_ = std::move(other.socket_path_);
        connected_ = other.connected_;
        other.socket_fd_ = -1;
        other.connected_ = false;
    }
    return *this;
}

bool fd_receiver::connect(const char *uuid) {
    if (connected_) {
        return true;
    }
    if (!uuid) {
        return false;
    }

    socket_path_ = "/tmp/nozzle_fd_";
    socket_path_ += uuid;

    socket_fd_ = socket(AF_UNIX, SOCK_STREAM, 0);
    if (socket_fd_ < 0) {
        return false;
    }

    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, socket_path_.c_str(), sizeof(addr.sun_path) - 1);

    if (::connect(socket_fd_, reinterpret_cast<struct sockaddr *>(&addr),
                  sizeof(addr)) < 0) {
        close(socket_fd_);
        socket_fd_ = -1;
        socket_path_.clear();
        return false;
    }

    connected_ = true;
    return true;
}

void fd_receiver::disconnect() {
    if (socket_fd_ >= 0) {
        close(socket_fd_);
        socket_fd_ = -1;
    }
    socket_path_.clear();
    connected_ = false;
}

bool fd_receiver::recv_dmabuf_fds(int *fds, size_t count) {
    if (!connected_ || socket_fd_ < 0) {
        return false;
    }
    return recv_fds(socket_fd_, fds, count);
}

bool fd_receiver::is_connected() const {
    return connected_;
}

dmabuf_texture_cache::~dmabuf_texture_cache() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (size_t i = 0; i < entries_.size(); ++i) {
        if (valid_[i] && entries_[i].fd >= 0) {
            close(entries_[i].fd);
        }
    }
}

void dmabuf_texture_cache::store(
    uint32_t slot_index, int fd, uint32_t width, uint32_t height, uint32_t format
) {
    if (slot_index >= 8) {
        return;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (valid_[slot_index] && entries_[slot_index].fd >= 0) {
        close(entries_[slot_index].fd);
    }
    entries_[slot_index] = cache_entry{fd, width, height, format};
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

    return detail::make_texture_from_backend(
        native_texture, native_surface, desc.width, desc.height, 0);
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
