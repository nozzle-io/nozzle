// nozzle - pixel_access.cpp - CPU pixel access for frames

#include <nozzle/pixel_access.hpp>
#include <nozzle/result.hpp>

#if NOZZLE_PLATFORM_MACOS
#include <nozzle/backends/metal.hpp>
#include <IOSurface/IOSurface.h>
#elif NOZZLE_PLATFORM_LINUX
#include <nozzle/backends/linux.hpp>
#include <sys/mman.h>
#include <unistd.h>
#elif NOZZLE_PLATFORM_WINDOWS
#include <nozzle/backends/d3d11.hpp>
#include <d3d11.h>
#endif

namespace nozzle {

namespace {

uint32_t bytes_per_pixel(texture_format fmt) {
    switch (fmt) {
    case texture_format::r8_unorm: return 1;
    case texture_format::rg8_unorm: return 2;
    case texture_format::rgba8_unorm:
    case texture_format::bgra8_unorm:
    case texture_format::rgba8_srgb:
    case texture_format::bgra8_srgb:
    case texture_format::r16_unorm:
    case texture_format::r16_float: return 4;
    case texture_format::rg16_unorm:
    case texture_format::rg16_float: return 4;
    case texture_format::rgba16_unorm:
    case texture_format::rgba16_float: return 8;
    case texture_format::r32_float: return 4;
    case texture_format::rg32_float: return 8;
    case texture_format::rgba32_float: return 16;
    case texture_format::r32_uint: return 4;
    case texture_format::rgba32_uint: return 16;
    case texture_format::depth32_float: return 4;
    default: return 0;
    }
}

} // anonymous namespace

#if NOZZLE_PLATFORM_MACOS

Result<mapped_pixels> lock_frame_pixels(const frame &frm) {
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

    auto info = frm.info();
    return mapped_pixels{
        IOSurfaceGetBaseAddress(surface),
        static_cast<uint32_t>(IOSurfaceGetBytesPerRow(surface)),
        info.width,
        info.height,
        info.format
    };
}

void unlock_frame_pixels(const frame &frm) {
    if (!frm.valid()) { return; }

    auto &tex = frm.get_texture();
    void *surface_ptr = metal::get_io_surface(tex);
    if (!surface_ptr) { return; }

    IOSurfaceUnlock(static_cast<IOSurfaceRef>(surface_ptr), kIOSurfaceLockReadOnly, nullptr);
}

Result<mapped_pixels> lock_writable_pixels(writable_frame &frm) {
    if (!frm.valid()) {
        return Error{ErrorCode::InvalidArgument, "writable_frame is not valid"};
    }

    auto &tex = frm.get_texture();
    void *surface_ptr = metal::get_io_surface(tex);
    if (!surface_ptr) {
        return Error{ErrorCode::BackendError, "no IOSurface in writable_frame texture"};
    }

    IOSurfaceRef surface = static_cast<IOSurfaceRef>(surface_ptr);
    IOReturn ret = IOSurfaceLock(surface, 0, nullptr);
    if (ret != kIOReturnSuccess) {
        return Error{ErrorCode::BackendError, "IOSurfaceLock failed"};
    }

    auto desc = frm.desc();
    return mapped_pixels{
        IOSurfaceGetBaseAddress(surface),
        static_cast<uint32_t>(IOSurfaceGetBytesPerRow(surface)),
        desc.width,
        desc.height,
        desc.format
    };
}

void unlock_writable_pixels(writable_frame &frm) {
    if (!frm.valid()) { return; }

    auto &tex = frm.get_texture();
    void *surface_ptr = metal::get_io_surface(tex);
    if (!surface_ptr) { return; }

    IOSurfaceUnlock(static_cast<IOSurfaceRef>(surface_ptr), 0, nullptr);
}

#elif NOZZLE_PLATFORM_LINUX

namespace {

struct linux_lock_state {
    void *mapped_ptr{nullptr};
    size_t mapped_size{0};
};

thread_local linux_lock_state tl_read_state;
thread_local linux_lock_state tl_write_state;

} // anonymous namespace

Result<mapped_pixels> lock_frame_pixels(const frame &frm) {
    if (!frm.valid()) {
        return Error{ErrorCode::InvalidArgument, "frame is not valid"};
    }

    auto &tex = frm.get_texture();
    int fd = dma_buf::get_dmabuf_fd(tex);
    if (fd < 0) {
        return Error{ErrorCode::BackendError, "no DMA-BUF fd in frame texture"};
    }

    auto info = frm.info();
    uint32_t bpp = bytes_per_pixel(info.format);
    if (bpp == 0) {
        return Error{ErrorCode::UnsupportedFormat, "unsupported texture format for CPU access"};
    }

    size_t row_bytes = static_cast<size_t>(info.width) * bpp;
    size_t map_size = row_bytes * info.height;

    void *ptr = mmap(nullptr, map_size, PROT_READ, MAP_SHARED, fd, 0);
    if (ptr == MAP_FAILED) {
        return Error{ErrorCode::BackendError, "mmap of DMA-BUF fd failed"};
    }

    tl_read_state = linux_lock_state{ptr, map_size};

    return mapped_pixels{
        ptr,
        static_cast<uint32_t>(row_bytes),
        info.width,
        info.height,
        info.format
    };
}

void unlock_frame_pixels(const frame &) {
    if (tl_read_state.mapped_ptr) {
        munmap(tl_read_state.mapped_ptr, tl_read_state.mapped_size);
        tl_read_state = linux_lock_state{};
    }
}

Result<mapped_pixels> lock_writable_pixels(writable_frame &frm) {
    if (!frm.valid()) {
        return Error{ErrorCode::InvalidArgument, "writable_frame is not valid"};
    }

    auto &tex = frm.get_texture();
    int fd = dma_buf::get_dmabuf_fd(tex);
    if (fd < 0) {
        return Error{ErrorCode::BackendError, "no DMA-BUF fd in writable_frame texture"};
    }

    auto desc = frm.desc();
    uint32_t bpp = bytes_per_pixel(desc.format);
    if (bpp == 0) {
        return Error{ErrorCode::UnsupportedFormat, "unsupported texture format for CPU access"};
    }

    size_t row_bytes = static_cast<size_t>(desc.width) * bpp;
    size_t map_size = row_bytes * desc.height;

    void *ptr = mmap(nullptr, map_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (ptr == MAP_FAILED) {
        return Error{ErrorCode::BackendError, "mmap of DMA-BUF fd failed"};
    }

    tl_write_state = linux_lock_state{ptr, map_size};

    return mapped_pixels{
        ptr,
        static_cast<uint32_t>(row_bytes),
        desc.width,
        desc.height,
        desc.format
    };
}

void unlock_writable_pixels(writable_frame &) {
    if (tl_write_state.mapped_ptr) {
        munmap(tl_write_state.mapped_ptr, tl_write_state.mapped_size);
        tl_write_state = linux_lock_state{};
    }
}

#elif NOZZLE_PLATFORM_WINDOWS

namespace {

struct windows_read_state {
    ID3D11Texture2D *staging{nullptr};
    ID3D11DeviceContext *context{nullptr};
    ID3D11Device *device{nullptr};
};

struct windows_write_state {
    ID3D11Texture2D *source{nullptr};
    ID3D11Texture2D *staging{nullptr};
    ID3D11DeviceContext *context{nullptr};
    ID3D11Device *device{nullptr};
};

thread_local windows_read_state tl_read_state;
thread_local windows_write_state tl_write_state;

} // anonymous namespace

Result<mapped_pixels> lock_frame_pixels(const frame &frm) {
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

    D3D11_TEXTURE2D_DESC tex_desc{};
    d3d_tex->GetDesc(&tex_desc);
    tex_desc.Usage = D3D11_USAGE_STAGING;
    tex_desc.BindFlags = 0;
    tex_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    tex_desc.MiscFlags = 0;

    ID3D11Texture2D *staging = nullptr;
    HRESULT hr = device->CreateTexture2D(&tex_desc, nullptr, &staging);
    if (FAILED(hr) || !staging) {
        ctx->Release();
        device->Release();
        return Error{ErrorCode::ResourceCreationFailed, "failed to create staging texture"};
    }

    ctx->CopySubresourceRegion(staging, 0, 0, 0, 0, d3d_tex, 0, nullptr);

    D3D11_MAPPED_SUBRESOURCE mapped{};
    hr = ctx->Map(staging, 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr)) {
        staging->Release();
        ctx->Release();
        device->Release();
        return Error{ErrorCode::BackendError, "failed to map staging texture"};
    }

    tl_read_state = windows_read_state{staging, ctx, device};

    auto info = frm.info();
    return mapped_pixels{
        mapped.pData,
        static_cast<uint32_t>(mapped.RowPitch),
        info.width,
        info.height,
        info.format
    };
}

void unlock_frame_pixels(const frame &) {
    if (tl_read_state.staging) {
        tl_read_state.context->Unmap(tl_read_state.staging, 0);
        tl_read_state.staging->Release();
        tl_read_state.context->Release();
        tl_read_state.device->Release();
        tl_read_state = windows_read_state{};
    }
}

Result<mapped_pixels> lock_writable_pixels(writable_frame &frm) {
    if (!frm.valid()) {
        return Error{ErrorCode::InvalidArgument, "writable_frame is not valid"};
    }

    auto &tex = frm.get_texture();
    ID3D11Texture2D *d3d_tex = d3d11::get_texture(tex);
    if (!d3d_tex) {
        return Error{ErrorCode::BackendError, "no D3D11 texture in writable_frame"};
    }

    ID3D11Device *device = nullptr;
    d3d_tex->GetDevice(&device);
    if (!device) {
        return Error{ErrorCode::BackendError, "failed to get D3D11 device from texture"};
    }

    ID3D11DeviceContext *ctx = nullptr;
    device->GetImmediateContext(&ctx);

    D3D11_TEXTURE2D_DESC tex_desc{};
    d3d_tex->GetDesc(&tex_desc);
    tex_desc.Usage = D3D11_USAGE_STAGING;
    tex_desc.BindFlags = 0;
    tex_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    tex_desc.MiscFlags = 0;

    ID3D11Texture2D *staging = nullptr;
    HRESULT hr = device->CreateTexture2D(&tex_desc, nullptr, &staging);
    if (FAILED(hr) || !staging) {
        ctx->Release();
        device->Release();
        return Error{ErrorCode::ResourceCreationFailed, "failed to create staging texture"};
    }

    D3D11_MAPPED_SUBRESOURCE mapped{};
    hr = ctx->Map(staging, 0, D3D11_MAP_WRITE, 0, &mapped);
    if (FAILED(hr)) {
        staging->Release();
        ctx->Release();
        device->Release();
        return Error{ErrorCode::BackendError, "failed to map staging texture"};
    }

    tl_write_state = windows_write_state{d3d_tex, staging, ctx, device};

    auto desc = frm.desc();
    return mapped_pixels{
        mapped.pData,
        static_cast<uint32_t>(mapped.RowPitch),
        desc.width,
        desc.height,
        desc.format
    };
}

void unlock_writable_pixels(writable_frame &) {
    if (tl_write_state.staging) {
        tl_write_state.context->Unmap(tl_write_state.staging, 0);
        tl_write_state.context->CopySubresourceRegion(
            tl_write_state.source, 0, 0, 0, 0,
            tl_write_state.staging, 0, nullptr);
        tl_write_state.staging->Release();
        tl_write_state.context->Release();
        tl_write_state.device->Release();
        tl_write_state = windows_write_state{};
    }
}

#else

Result<mapped_pixels> lock_frame_pixels(const frame &) {
    return Error{ErrorCode::UnsupportedBackend, "pixel access not implemented for this platform"};
}

void unlock_frame_pixels(const frame &) {}

Result<mapped_pixels> lock_writable_pixels(writable_frame &) {
    return Error{ErrorCode::UnsupportedBackend, "pixel access not implemented for this platform"};
}

void unlock_writable_pixels(writable_frame &) {}

#endif

} // namespace nozzle
