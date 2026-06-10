// nozzle - pixel_access.cpp - CPU pixel access for frames

#include <nozzle/pixel_access.hpp>
#include <nozzle/result.hpp>
#include <nozzle/format_resolve.hpp>

#include "frame_helpers.hpp"

#include <memory>
#include <new>
#include <utility>

#if NOZZLE_PLATFORM_MACOS
#include <nozzle/backends/metal.hpp>
#include <CoreFoundation/CoreFoundation.h>
#include <IOSurface/IOSurface.h>
#elif NOZZLE_PLATFORM_LINUX
#include <nozzle/backends/linux.hpp>
#include <sys/mman.h>
#include <unistd.h>
#elif NOZZLE_PLATFORM_WINDOWS
#include <nozzle/backends/d3d11.hpp>
#include <d3d10_1.h>
#include <d3d11.h>
#include "backends/d3d11/d3d11_helpers.hpp"
#endif

namespace nozzle {

namespace {

uint32_t bytes_per_pixel(texture_format fmt) {
    switch (fmt) {
    case texture_format::r8_unorm: return 1;
    case texture_format::rg8_unorm: return 2;
    case texture_format::rgb8_unorm: return 3;
    case texture_format::r16_unorm:
    case texture_format::r16_float: return 2;
    case texture_format::rgba8_unorm:
    case texture_format::bgra8_unorm:
    case texture_format::rgba8_srgb:
    case texture_format::bgra8_srgb:
    case texture_format::r32_float:
    case texture_format::r32_uint: return 4;
    case texture_format::rg16_unorm:
    case texture_format::rg16_float: return 4;
    case texture_format::rgb16_unorm:
    case texture_format::rgb16_float: return 6;
    case texture_format::rg32_float: return 8;
    case texture_format::rgba16_unorm:
    case texture_format::rgba16_float: return 8;
    case texture_format::rgb32_float:
    case texture_format::rgb32_uint: return 12;
    case texture_format::rgba32_float:
    case texture_format::rgba32_uint: return 16;
    case texture_format::depth32_float: return 4;
    default: return 0;
    }
}

} // anonymous namespace

struct pixel_mapping::Impl {
    mapped_pixels pixels{};
    bool is_writable{false};

    virtual ~Impl() noexcept = default;
    virtual Result<void> unlock_checked() noexcept = 0;
};

pixel_mapping::pixel_mapping() noexcept = default;
pixel_mapping::~pixel_mapping() noexcept {
    unlock();
}

pixel_mapping::pixel_mapping(std::unique_ptr<Impl> impl) noexcept
    : impl_{std::move(impl)}
{}

pixel_mapping::pixel_mapping(pixel_mapping &&other) noexcept = default;

pixel_mapping &pixel_mapping::operator=(pixel_mapping &&other) noexcept {
    if (this != &other) {
        unlock();
        impl_ = std::move(other.impl_);
    }
    return *this;
}

bool pixel_mapping::valid() const noexcept {
    return impl_ != nullptr;
}

bool pixel_mapping::writable() const noexcept {
    return impl_ && impl_->is_writable;
}

const mapped_pixels &pixel_mapping::pixels() const noexcept {
    static const mapped_pixels empty{};
    if (!impl_) {
        return empty;
    }
    return impl_->pixels;
}

Result<void> pixel_mapping::unlock_checked() noexcept {
    if (!impl_) {
        return Error{ErrorCode::InvalidArgument, "no active pixel mapping"};
    }

    std::unique_ptr<Impl> impl = std::move(impl_);
    return impl->unlock_checked();
}

void pixel_mapping::unlock() noexcept {
    if (!impl_) {
        return;
    }
    (void)unlock_checked();
}

namespace {

Result<pixel_mapping> make_pixel_mapping(pixel_mapping::Impl *impl) {
    if (!impl) {
        return Error{ErrorCode::ResourceCreationFailed, "failed to allocate pixel mapping"};
    }
    return pixel_mapping{std::unique_ptr<pixel_mapping::Impl>(impl)};
}

} // anonymous namespace

#if NOZZLE_PLATFORM_MACOS

namespace {

struct macos_read_mapping final : pixel_mapping::Impl {
    IOSurfaceRef surface{nullptr};

    explicit macos_read_mapping(IOSurfaceRef surface_ref) noexcept
        : surface{surface_ref}
    {
        is_writable = false;
    }

    Result<void> unlock_checked() noexcept override {
        if (!surface) {
            return Error{ErrorCode::InvalidArgument, "no active pixel mapping"};
        }
        IOSurfaceRef surface_ref = surface;
        surface = nullptr;
        IOReturn ret = IOSurfaceUnlock(surface_ref, kIOSurfaceLockReadOnly, nullptr);
        CFRelease(surface_ref);
        if (ret != kIOReturnSuccess) {
            return Error{ErrorCode::BackendError, "IOSurfaceUnlock failed"};
        }
        return {};
    }
};

struct macos_write_mapping final : pixel_mapping::Impl {
    IOSurfaceRef surface{nullptr};
    detail::writable_cpu_mapping_state_ref mapping_state{};

    macos_write_mapping(
        IOSurfaceRef surface_ref,
        detail::writable_cpu_mapping_state_ref state_ref
    ) noexcept
        : surface{surface_ref}
        , mapping_state{std::move(state_ref)}
    {
        is_writable = true;
    }

    Result<void> unlock_checked() noexcept override {
        if (!surface) {
            return Error{ErrorCode::InvalidArgument, "no active pixel mapping"};
        }
        IOSurfaceRef surface_ref = surface;
        surface = nullptr;
        IOReturn ret = IOSurfaceUnlock(surface_ref, 0, nullptr);
        CFRelease(surface_ref);
        if (ret != kIOReturnSuccess) {
            detail::mark_writable_cpu_mapping_unlock_failed(mapping_state);
            return Error{ErrorCode::BackendError, "IOSurfaceUnlock failed"};
        }
        detail::mark_writable_cpu_mapping_unlocked(mapping_state);
        return {};
    }
};

} // anonymous namespace

Result<pixel_mapping> lock_frame_pixels_mapping_with_origin(const frame &frm, texture_origin desired_origin) {
    if (!frm.valid()) {
        return Error{ErrorCode::InvalidArgument, "frame is not valid"};
    }

    auto &tex = frm.get_texture();
    void *surface_ptr = metal::get_io_surface(tex);
    if (!surface_ptr) {
        return Error{ErrorCode::BackendError, "no IOSurface in frame texture"};
    }

    IOSurfaceRef surface = static_cast<IOSurfaceRef>(surface_ptr);
    IOReturn ret = IOSurfaceLock(surface, kIOSurfaceLockReadOnly, nullptr);
    if (ret != kIOReturnSuccess) {
        return Error{ErrorCode::BackendError, "IOSurfaceLock failed"};
    }
    CFRetain(surface);

    auto *mapping_impl = new (std::nothrow) macos_read_mapping(surface);
    if (!mapping_impl) {
        IOSurfaceUnlock(surface, kIOSurfaceLockReadOnly, nullptr);
        CFRelease(surface);
        return Error{ErrorCode::ResourceCreationFailed, "failed to allocate pixel mapping"};
    }

    auto info = frm.info();
    uint8_t *base = static_cast<uint8_t *>(IOSurfaceGetBaseAddress(surface));
    int64_t row_bytes = static_cast<int64_t>(IOSurfaceGetBytesPerRow(surface));

    auto resolved = tex.resolved();
    texture_format actual_fmt = resolved.storage_format;
    if (actual_fmt == texture_format::unknown) {
        actual_fmt = info.format;
    }

    if (desired_origin == texture_origin::top_left) {
        mapping_impl->pixels = mapped_pixels{base, row_bytes, info.width, info.height, actual_fmt, texture_origin::top_left, resolved.cpu_layout, resolved.source};
    } else {
        mapping_impl->pixels = mapped_pixels{
            base + static_cast<int64_t>(info.height - 1) * row_bytes,
            -row_bytes,
            info.width, info.height, actual_fmt, texture_origin::bottom_left,
            resolved.cpu_layout, resolved.source
        };
    }
    return make_pixel_mapping(mapping_impl);
}

Result<pixel_mapping> lock_writable_pixels_mapping_with_origin(writable_frame &frm, texture_origin desired_origin) {
    if (!frm.valid()) {
        return Error{ErrorCode::InvalidArgument, "writable_frame is not valid"};
    }

    auto &tex = frm.get_texture();
    void *surface_ptr = metal::get_io_surface(tex);
    if (!surface_ptr) {
        return Error{ErrorCode::BackendError, "no IOSurface in writable_frame texture"};
    }

    auto state_result = detail::begin_writable_frame_cpu_mapping(frm);
    if (!state_result.ok()) {
        return state_result.error();
    }
    auto mapping_state = std::move(state_result.value());

    IOSurfaceRef surface = static_cast<IOSurfaceRef>(surface_ptr);
    IOReturn ret = IOSurfaceLock(surface, 0, nullptr);
    if (ret != kIOReturnSuccess) {
        detail::mark_writable_cpu_mapping_unlocked(mapping_state);
        return Error{ErrorCode::BackendError, "IOSurfaceLock failed"};
    }
    CFRetain(surface);

    auto *mapping_impl = new (std::nothrow) macos_write_mapping(surface, std::move(mapping_state));
    if (!mapping_impl) {
        IOSurfaceUnlock(surface, 0, nullptr);
        CFRelease(surface);
        detail::mark_writable_cpu_mapping_unlocked(mapping_state);
        return Error{ErrorCode::ResourceCreationFailed, "failed to allocate pixel mapping"};
    }

    auto desc = frm.desc();
    uint8_t *base = static_cast<uint8_t *>(IOSurfaceGetBaseAddress(surface));
    int64_t row_bytes = static_cast<int64_t>(IOSurfaceGetBytesPerRow(surface));

    auto resolved = tex.resolved();
    texture_format actual_fmt = resolved.storage_format;
    if (actual_fmt == texture_format::unknown) {
        actual_fmt = desc.format;
    }

    if (desired_origin == texture_origin::top_left) {
        mapping_impl->pixels = mapped_pixels{base, row_bytes, desc.width, desc.height, actual_fmt, texture_origin::top_left, resolved.cpu_layout, resolved.source};
    } else {
        mapping_impl->pixels = mapped_pixels{
            base + static_cast<int64_t>(desc.height - 1) * row_bytes,
            -row_bytes,
            desc.width, desc.height, actual_fmt, texture_origin::bottom_left,
            resolved.cpu_layout, resolved.source
        };
    }
    return make_pixel_mapping(mapping_impl);
}

#elif NOZZLE_PLATFORM_LINUX

namespace {

struct linux_mapping final : pixel_mapping::Impl {
    void *mapped_ptr{nullptr};
    size_t mapped_size{0};
    detail::writable_cpu_mapping_state_ref mapping_state{};

    linux_mapping(
        void *ptr,
        size_t size,
        bool writable,
        detail::writable_cpu_mapping_state_ref state_ref = {}
    ) noexcept
        : mapped_ptr{ptr}
        , mapped_size{size}
        , mapping_state{std::move(state_ref)}
    {
        is_writable = writable;
    }

    Result<void> unlock_checked() noexcept override {
        if (!mapped_ptr || mapped_size == 0) {
            return Error{ErrorCode::InvalidArgument, "no active pixel mapping"};
        }

        void *ptr = mapped_ptr;
        size_t size = mapped_size;
        mapped_ptr = nullptr;
        mapped_size = 0;

        int rc = munmap(ptr, size);
        if (rc != 0) {
            if (is_writable) {
                detail::mark_writable_cpu_mapping_unlock_failed(mapping_state);
            }
            return Error{ErrorCode::BackendError, "munmap failed"};
        }
        if (is_writable) {
            detail::mark_writable_cpu_mapping_unlocked(mapping_state);
        }
        return {};
    }
};

} // anonymous namespace

Result<pixel_mapping> lock_frame_pixels_mapping_with_origin(const frame &frm, texture_origin desired_origin) {
    if (!frm.valid()) {
        return Error{ErrorCode::InvalidArgument, "frame is not valid"};
    }

    auto &tex = frm.get_texture();
    int fd = dma_buf::get_dmabuf_fd(tex);
    if (fd < 0) {
        return Error{ErrorCode::BackendError, "no DMA-BUF fd in frame"};
    }

    auto info = frm.info();
    uint32_t bpp = bytes_per_pixel(info.format);
    if (bpp == 0) {
        return Error{ErrorCode::UnsupportedFormat, "unsupported pixel format for CPU map"};
    }
    size_t row_bytes = static_cast<size_t>(info.width) * bpp;
    size_t map_size = row_bytes * static_cast<size_t>(info.height);
    void *ptr = mmap(nullptr, map_size, PROT_READ, MAP_SHARED, fd, 0);
    if (ptr == MAP_FAILED) {
        return Error{ErrorCode::BackendError, "mmap failed"};
    }

    auto *mapping_impl = new (std::nothrow) linux_mapping(ptr, map_size, false);
    if (!mapping_impl) {
        munmap(ptr, map_size);
        return Error{ErrorCode::ResourceCreationFailed, "failed to allocate pixel mapping"};
    }

    auto *base = static_cast<uint8_t *>(ptr);
    int64_t stride = static_cast<int64_t>(row_bytes);
    auto resolved = tex.resolved();

    if (desired_origin == texture_origin::top_left) {
        mapping_impl->pixels = mapped_pixels{base, stride, info.width, info.height, info.format, texture_origin::top_left, resolved.cpu_layout, resolved.source};
    } else {
        mapping_impl->pixels = mapped_pixels{
            base + static_cast<int64_t>(info.height - 1) * stride,
            -stride,
            info.width, info.height, info.format, texture_origin::bottom_left,
            resolved.cpu_layout, resolved.source
        };
    }
    return make_pixel_mapping(mapping_impl);
}

Result<pixel_mapping> lock_writable_pixels_mapping_with_origin(writable_frame &frm, texture_origin desired_origin) {
    if (!frm.valid()) {
        return Error{ErrorCode::InvalidArgument, "writable_frame is not valid"};
    }

    auto &tex = frm.get_texture();
    int fd = dma_buf::get_dmabuf_fd(tex);
    if (fd < 0) {
        return Error{ErrorCode::BackendError, "no DMA-BUF fd in writable_frame"};
    }

    auto state_result = detail::begin_writable_frame_cpu_mapping(frm);
    if (!state_result.ok()) {
        return state_result.error();
    }
    auto mapping_state = std::move(state_result.value());

    auto desc = frm.desc();
    uint32_t bpp = bytes_per_pixel(desc.format);
    if (bpp == 0) {
        detail::mark_writable_cpu_mapping_unlocked(mapping_state);
        return Error{ErrorCode::UnsupportedFormat, "unsupported pixel format for CPU map"};
    }
    size_t row_bytes = static_cast<size_t>(desc.width) * bpp;
    size_t map_size = row_bytes * static_cast<size_t>(desc.height);
    void *ptr = mmap(nullptr, map_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (ptr == MAP_FAILED) {
        detail::mark_writable_cpu_mapping_unlocked(mapping_state);
        return Error{ErrorCode::BackendError, "mmap failed"};
    }

    auto *mapping_impl = new (std::nothrow) linux_mapping(ptr, map_size, true, std::move(mapping_state));
    if (!mapping_impl) {
        munmap(ptr, map_size);
        detail::mark_writable_cpu_mapping_unlocked(mapping_state);
        return Error{ErrorCode::ResourceCreationFailed, "failed to allocate pixel mapping"};
    }

    auto *base = static_cast<uint8_t *>(ptr);
    int64_t stride = static_cast<int64_t>(row_bytes);
    auto resolved = tex.resolved();

    if (desired_origin == texture_origin::top_left) {
        mapping_impl->pixels = mapped_pixels{base, stride, desc.width, desc.height, desc.format, texture_origin::top_left, resolved.cpu_layout, resolved.source};
    } else {
        mapping_impl->pixels = mapped_pixels{
            base + static_cast<int64_t>(desc.height - 1) * stride,
            -stride,
            desc.width, desc.height, desc.format, texture_origin::bottom_left,
            resolved.cpu_layout, resolved.source
        };
    }
    return make_pixel_mapping(mapping_impl);
}

#elif NOZZLE_PLATFORM_WINDOWS

namespace {

void enter_multithread(ID3D10Multithread *multithread) noexcept {
    if (multithread) {
        multithread->Enter();
    }
}

void leave_multithread(ID3D10Multithread *multithread) noexcept {
    if (multithread) {
        multithread->Leave();
    }
}

ID3D10Multithread *make_multithread_guard(ID3D11DeviceContext *context) noexcept {
    if (!context) {
        return nullptr;
    }
    ID3D10Multithread *multithread = nullptr;
    HRESULT hr = context->QueryInterface(
        __uuidof(ID3D10Multithread), reinterpret_cast<void **>(&multithread));
    if (FAILED(hr) || !multithread) {
        return nullptr;
    }
    multithread->SetMultithreadProtected(TRUE);
    return multithread;
}

struct windows_read_mapping final : pixel_mapping::Impl {
    ID3D11Texture2D *staging{nullptr};
    ID3D11DeviceContext *context{nullptr};
    ID3D11Device *device{nullptr};
    ID3D10Multithread *multithread{nullptr};

    windows_read_mapping(
        ID3D11Texture2D *staging_texture,
        ID3D11DeviceContext *device_context,
        ID3D11Device *d3d_device,
        ID3D10Multithread *multithread_guard
    ) noexcept
        : staging{staging_texture}
        , context{device_context}
        , device{d3d_device}
        , multithread{multithread_guard}
    {
        is_writable = false;
    }

    Result<void> unlock_checked() noexcept override {
        if (!staging || !context) {
            return Error{ErrorCode::InvalidArgument, "no active pixel mapping"};
        }

        ID3D11Texture2D *staging_texture = staging;
        ID3D11DeviceContext *device_context = context;
        ID3D11Device *d3d_device = device;
        ID3D10Multithread *multithread_guard = multithread;
        staging = nullptr;
        context = nullptr;
        device = nullptr;
        multithread = nullptr;

        enter_multithread(multithread_guard);
        device_context->Unmap(staging_texture, 0);
        leave_multithread(multithread_guard);

        if (multithread_guard) {
            multithread_guard->Release();
        }
        staging_texture->Release();
        device_context->Release();
        if (d3d_device) {
            d3d_device->Release();
        }
        return {};
    }
};

struct windows_write_mapping final : pixel_mapping::Impl {
    ID3D11Texture2D *source{nullptr};
    ID3D11Texture2D *staging{nullptr};
    ID3D11DeviceContext *context{nullptr};
    ID3D11Device *device{nullptr};
    IDXGIKeyedMutex *publish_mutex{nullptr};
    ID3D10Multithread *multithread{nullptr};
    detail::writable_cpu_mapping_state_ref mapping_state{};

    windows_write_mapping(
        ID3D11Texture2D *source_texture,
        ID3D11Texture2D *staging_texture,
        ID3D11DeviceContext *device_context,
        ID3D11Device *d3d_device,
        IDXGIKeyedMutex *keyed_mutex,
        ID3D10Multithread *multithread_guard,
        detail::writable_cpu_mapping_state_ref state_ref
    ) noexcept
        : source{source_texture}
        , staging{staging_texture}
        , context{device_context}
        , device{d3d_device}
        , publish_mutex{keyed_mutex}
        , multithread{multithread_guard}
        , mapping_state{std::move(state_ref)}
    {
        is_writable = true;
    }

    Result<void> unlock_checked() noexcept override {
        if (!source || !staging || !context) {
            return Error{ErrorCode::InvalidArgument, "no active pixel mapping"};
        }

        ID3D11Texture2D *source_texture = source;
        ID3D11Texture2D *staging_texture = staging;
        ID3D11DeviceContext *device_context = context;
        ID3D11Device *d3d_device = device;
        IDXGIKeyedMutex *keyed_mutex = publish_mutex;
        ID3D10Multithread *multithread_guard = multithread;
        source = nullptr;
        staging = nullptr;
        context = nullptr;
        device = nullptr;
        publish_mutex = nullptr;
        multithread = nullptr;

        HRESULT release_hr = S_OK;

        enter_multithread(multithread_guard);
        device_context->Unmap(staging_texture, 0);
        device_context->CopySubresourceRegion(
            source_texture, 0, 0, 0, 0,
            staging_texture, 0, nullptr);
        device_context->Flush();
        leave_multithread(multithread_guard);

        if (keyed_mutex) {
            release_hr = keyed_mutex->ReleaseSync(1);
            keyed_mutex->Release();
        }
        if (multithread_guard) {
            multithread_guard->Release();
        }
        staging_texture->Release();
        device_context->Release();
        if (d3d_device) {
            d3d_device->Release();
        }
        source_texture->Release();

        if (FAILED(release_hr)) {
            detail::mark_writable_cpu_mapping_unlock_failed(mapping_state);
            return Error{ErrorCode::BackendError, "failed to release D3D11 writable keyed mutex"};
        }
        detail::mark_writable_cpu_mapping_unlocked(mapping_state);
        return {};
    }
};

} // anonymous namespace

Result<pixel_mapping> lock_frame_pixels_mapping_with_origin(const frame &frm, texture_origin desired_origin) {
    if (!frm.valid()) {
        return Error{ErrorCode::InvalidArgument, "frame is not valid"};
    }

    auto &tex = frm.get_texture();
    ID3D11Texture2D *d3d_tex = d3d11::get_texture(tex);
    if (!d3d_tex) {
        return Error{ErrorCode::BackendError, "no D3D11 texture in frame"};
    }

    ID3D11Device *device = nullptr;
    d3d_tex->GetDevice(&device);
    if (!device) {
        return Error{ErrorCode::BackendError, "failed to get D3D11 device from texture"};
    }

    ID3D11DeviceContext *ctx = nullptr;
    device->GetImmediateContext(&ctx);
    if (!ctx) {
        device->Release();
        return Error{ErrorCode::BackendError, "failed to get D3D11 immediate context"};
    }
    ID3D10Multithread *multithread = make_multithread_guard(ctx);
    if (!multithread) {
        ctx->Release();
        device->Release();
        return Error{ErrorCode::UnsupportedBackend, "D3D11 multithread protection is unavailable"};
    }

    D3D11_TEXTURE2D_DESC tex_desc{};
    d3d_tex->GetDesc(&tex_desc);
    tex_desc.Usage = D3D11_USAGE_STAGING;
    tex_desc.BindFlags = 0;
    tex_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    tex_desc.MiscFlags = 0;

    ID3D11Texture2D *staging = nullptr;
    HRESULT hr = device->CreateTexture2D(&tex_desc, nullptr, &staging);
    if (FAILED(hr) || !staging) {
        if (multithread) {
            multithread->Release();
        }
        ctx->Release();
        device->Release();
        return Error{ErrorCode::ResourceCreationFailed, "failed to create staging texture"};
    }

    enter_multithread(multithread);
    ctx->CopySubresourceRegion(staging, 0, 0, 0, 0, d3d_tex, 0, nullptr);

    D3D11_MAPPED_SUBRESOURCE mapped{};
    hr = ctx->Map(staging, 0, D3D11_MAP_READ, 0, &mapped);
    leave_multithread(multithread);
    if (FAILED(hr)) {
        staging->Release();
        if (multithread) {
            multithread->Release();
        }
        ctx->Release();
        device->Release();
        return Error{ErrorCode::BackendError, "failed to map staging texture"};
    }

    auto *mapping_impl = new (std::nothrow) windows_read_mapping(staging, ctx, device, multithread);
    if (!mapping_impl) {
        enter_multithread(multithread);
        ctx->Unmap(staging, 0);
        leave_multithread(multithread);
        staging->Release();
        if (multithread) {
            multithread->Release();
        }
        ctx->Release();
        device->Release();
        return Error{ErrorCode::ResourceCreationFailed, "failed to allocate pixel mapping"};
    }

    auto info = frm.info();
    auto *base = static_cast<uint8_t *>(mapped.pData);
    int64_t stride = static_cast<int64_t>(mapped.RowPitch);
    auto resolved = tex.resolved();

    if (desired_origin == texture_origin::top_left) {
        mapping_impl->pixels = mapped_pixels{base, stride, info.width, info.height, info.format, texture_origin::top_left, resolved.cpu_layout, resolved.source};
    } else {
        mapping_impl->pixels = mapped_pixels{
            base + static_cast<int64_t>(info.height - 1) * stride,
            -stride,
            info.width, info.height, info.format, texture_origin::bottom_left,
            resolved.cpu_layout, resolved.source
        };
    }
    return make_pixel_mapping(mapping_impl);
}

Result<pixel_mapping> lock_writable_pixels_mapping_with_origin(writable_frame &frm, texture_origin desired_origin) {
    if (!frm.valid()) {
        return Error{ErrorCode::InvalidArgument, "writable_frame is not valid"};
    }

    auto &tex = frm.get_texture();
    ID3D11Texture2D *d3d_tex = d3d11::get_texture(tex);
    if (!d3d_tex) {
        return Error{ErrorCode::BackendError, "no D3D11 texture in writable_frame"};
    }

    auto state_result = detail::begin_writable_frame_cpu_mapping(frm);
    if (!state_result.ok()) {
        return state_result.error();
    }
    auto mapping_state = std::move(state_result.value());

    ID3D11Device *device = nullptr;
    d3d_tex->GetDevice(&device);
    if (!device) {
        detail::mark_writable_cpu_mapping_unlocked(mapping_state);
        return Error{ErrorCode::BackendError, "failed to get D3D11 device from texture"};
    }

    ID3D11DeviceContext *ctx = nullptr;
    device->GetImmediateContext(&ctx);
    if (!ctx) {
        device->Release();
        detail::mark_writable_cpu_mapping_unlocked(mapping_state);
        return Error{ErrorCode::BackendError, "failed to get D3D11 immediate context"};
    }
    ID3D10Multithread *multithread = make_multithread_guard(ctx);
    if (!multithread) {
        ctx->Release();
        device->Release();
        detail::mark_writable_cpu_mapping_unlocked(mapping_state);
        return Error{ErrorCode::UnsupportedBackend, "D3D11 multithread protection is unavailable"};
    }

    D3D11_TEXTURE2D_DESC tex_desc{};
    d3d_tex->GetDesc(&tex_desc);
    tex_desc.Usage = D3D11_USAGE_STAGING;
    tex_desc.BindFlags = 0;
    tex_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    tex_desc.MiscFlags = 0;

    ID3D11Texture2D *staging = nullptr;
    HRESULT hr = device->CreateTexture2D(&tex_desc, nullptr, &staging);
    if (FAILED(hr) || !staging) {
        if (multithread) {
            multithread->Release();
        }
        ctx->Release();
        device->Release();
        detail::mark_writable_cpu_mapping_unlocked(mapping_state);
        return Error{ErrorCode::ResourceCreationFailed, "failed to create staging texture"};
    }

    D3D11_MAPPED_SUBRESOURCE mapped{};
    enter_multithread(multithread);
    hr = ctx->Map(staging, 0, D3D11_MAP_WRITE, 0, &mapped);
    leave_multithread(multithread);
    if (FAILED(hr)) {
        staging->Release();
        if (multithread) {
            multithread->Release();
        }
        ctx->Release();
        device->Release();
        detail::mark_writable_cpu_mapping_unlocked(mapping_state);
        return Error{ErrorCode::BackendError, "failed to map staging texture"};
    }

    IDXGIKeyedMutex *mutex = nullptr;
    HRESULT mutex_hr = d3d_tex->QueryInterface(
        __uuidof(IDXGIKeyedMutex), reinterpret_cast<void **>(&mutex));
    if (SUCCEEDED(mutex_hr) && mutex) {
        HRESULT acquire_hr = d3d11::acquire_publish_mutex(mutex);
        if (acquire_hr != S_OK) {
            mutex->Release();
            enter_multithread(multithread);
            ctx->Unmap(staging, 0);
            leave_multithread(multithread);
            staging->Release();
            if (multithread) {
                multithread->Release();
            }
            ctx->Release();
            device->Release();
            detail::mark_writable_cpu_mapping_unlocked(mapping_state);
            return Error{ErrorCode::Timeout, "timeout acquiring D3D11 writable keyed mutex"};
        }
    }

    d3d_tex->AddRef();
    auto *mapping_impl = new (std::nothrow) windows_write_mapping(
        d3d_tex, staging, ctx, device, mutex, multithread, std::move(mapping_state));
    if (!mapping_impl) {
        enter_multithread(multithread);
        ctx->Unmap(staging, 0);
        leave_multithread(multithread);
        if (mutex) {
            (void)mutex->ReleaseSync(1);
            mutex->Release();
        }
        d3d_tex->Release();
        staging->Release();
        if (multithread) {
            multithread->Release();
        }
        ctx->Release();
        device->Release();
        detail::mark_writable_cpu_mapping_unlocked(mapping_state);
        return Error{ErrorCode::ResourceCreationFailed, "failed to allocate pixel mapping"};
    }

    auto desc = frm.desc();
    auto *base = static_cast<uint8_t *>(mapped.pData);
    int64_t stride = static_cast<int64_t>(mapped.RowPitch);
    auto resolved = tex.resolved();

    if (desired_origin == texture_origin::top_left) {
        mapping_impl->pixels = mapped_pixels{base, stride, desc.width, desc.height, desc.format, texture_origin::top_left, resolved.cpu_layout, resolved.source};
    } else {
        mapping_impl->pixels = mapped_pixels{
            base + static_cast<int64_t>(desc.height - 1) * stride,
            -stride,
            desc.width, desc.height, desc.format, texture_origin::bottom_left,
            resolved.cpu_layout, resolved.source
        };
    }
    return make_pixel_mapping(mapping_impl);
}

#else

Result<pixel_mapping> lock_frame_pixels_mapping_with_origin(const frame &, texture_origin) {
    return Error{ErrorCode::UnsupportedBackend, "pixel access not implemented for this platform"};
}

Result<pixel_mapping> lock_writable_pixels_mapping_with_origin(writable_frame &, texture_origin) {
    return Error{ErrorCode::UnsupportedBackend, "pixel access not implemented for this platform"};
}

#endif

Result<mapped_pixels> lock_frame_pixels_with_origin(const frame &frm, texture_origin desired_origin) {
    return detail::lock_legacy_frame_pixels_with_origin(frm, desired_origin);
}

void unlock_frame_pixels(const frame &frm) {
    detail::unlock_legacy_frame_pixels(frm);
}

Result<mapped_pixels> lock_writable_pixels_with_origin(writable_frame &frm, texture_origin desired_origin) {
    return detail::lock_legacy_writable_pixels_with_origin(frm, desired_origin);
}

Result<void> unlock_writable_pixels_checked(writable_frame &frm) {
    return detail::unlock_legacy_writable_pixels_checked(frm);
}

void unlock_writable_pixels(writable_frame &frm) {
    (void)unlock_writable_pixels_checked(frm);
}

} // namespace nozzle
