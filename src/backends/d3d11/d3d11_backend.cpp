// nozzle - d3d11_backend.cpp - Windows D3D11 backend (shared HANDLE textures)

#if NOZZLE_HAS_D3D11

#include <bbb/nozzle/backends/d3d11.hpp>
#include <bbb/nozzle/result.hpp>

#include <d3d11.h>
#include <dxgi1_2.h>

namespace bbb {
namespace nozzle {
namespace d3d11 {

Result<device> wrap_device(const DeviceDesc &desc) {
    if (!desc.device) {
        return Error{ErrorCode::InvalidArgument, "device is null"};
    }
    desc.device->AddRef();
    return detail::make_device_from_backend(desc.device);
}

Result<texture> wrap_texture(const TextureWrapDesc &desc) {
    if (!desc.texture) {
        return Error{ErrorCode::InvalidArgument, "texture is null"};
    }
    desc.texture->AddRef();

    IDXGIResource *dxgi_resource = nullptr;
    HRESULT hr = desc.texture->QueryInterface(
        __uuidof(IDXGIResource), reinterpret_cast<void **>(&dxgi_resource));
    if (FAILED(hr) || !dxgi_resource) {
        desc.texture->Release();
        return Error{ErrorCode::BackendError, "texture is not a shared resource"};
    }

    HANDLE shared_handle = nullptr;
    hr = dxgi_resource->GetSharedHandle(&shared_handle);
    dxgi_resource->Release();

    if (FAILED(hr) || !shared_handle) {
        desc.texture->Release();
        return Error{ErrorCode::SharedHandleFailed, "failed to get shared handle"};
    }

    return detail::make_texture_from_backend(
        reinterpret_cast<void *>(desc.texture),
        reinterpret_cast<void *>(shared_handle),
        desc.width,
        desc.height,
        desc.dxgi_format
    );
}

ID3D11Texture2D *get_texture(const texture &tex) {
    void *native = detail::get_texture_native(tex);
    return static_cast<ID3D11Texture2D *>(native);
}

HANDLE get_shared_handle(const texture &tex) {
    void *native = detail::get_surface_native(tex);
    return reinterpret_cast<HANDLE>(reinterpret_cast<uintptr_t>(native));
}

void *get_default_d3d11_device() {
    ID3D11Device *device = nullptr;
    D3D_FEATURE_LEVEL feature_level;
    HRESULT hr = D3D11CreateDevice(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
        nullptr, 0, D3D11_SDK_VERSION, &device, &feature_level, nullptr);
    if (FAILED(hr)) {
        return nullptr;
    }
    return device;
}

void release_d3d11_device(void *device) {
    if (device) {
        static_cast<ID3D11Device *>(device)->Release();
    }
}

bool d3d11_supports_format(void *device, uint32_t format, uint32_t usage) {
    (void)usage;
    if (!device) {
        return false;
    }
    auto *d3d_device = static_cast<ID3D11Device *>(device);

    DXGI_FORMAT dxgi_fmt = DXGI_FORMAT_UNKNOWN;
    switch (static_cast<texture_format>(format)) {
        case texture_format::r8_unorm:         dxgi_fmt = DXGI_FORMAT_R8_UNORM; break;
        case texture_format::rg8_unorm:        dxgi_fmt = DXGI_FORMAT_R8G8_UNORM; break;
        case texture_format::rgba8_unorm:      dxgi_fmt = DXGI_FORMAT_R8G8B8A8_UNORM; break;
        case texture_format::bgra8_unorm:      dxgi_fmt = DXGI_FORMAT_B8G8R8A8_UNORM; break;
        case texture_format::rgba8_srgb:       dxgi_fmt = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB; break;
        case texture_format::bgra8_srgb:       dxgi_fmt = DXGI_FORMAT_B8G8R8A8_UNORM_SRGB; break;
        case texture_format::r16_unorm:        dxgi_fmt = DXGI_FORMAT_R16_UNORM; break;
        case texture_format::rg16_unorm:       dxgi_fmt = DXGI_FORMAT_R16G16_UNORM; break;
        case texture_format::rgba16_unorm:     dxgi_fmt = DXGI_FORMAT_R16G16B16A16_UNORM; break;
        case texture_format::r16_float:        dxgi_fmt = DXGI_FORMAT_R16_FLOAT; break;
        case texture_format::rg16_float:       dxgi_fmt = DXGI_FORMAT_R16G16_FLOAT; break;
        case texture_format::rgba16_float:     dxgi_fmt = DXGI_FORMAT_R16G16B16A16_FLOAT; break;
        case texture_format::r32_float:        dxgi_fmt = DXGI_FORMAT_R32_FLOAT; break;
        case texture_format::rg32_float:       dxgi_fmt = DXGI_FORMAT_R32G32_FLOAT; break;
        case texture_format::rgba32_float:     dxgi_fmt = DXGI_FORMAT_R32G32B32A32_FLOAT; break;
        case texture_format::r32_uint:         dxgi_fmt = DXGI_FORMAT_R32_UINT; break;
        case texture_format::rgba32_uint:      dxgi_fmt = DXGI_FORMAT_R32G32B32A32_UINT; break;
        case texture_format::depth32_float:    dxgi_fmt = DXGI_FORMAT_D32_FLOAT; break;
        default:                               return false;
    }

    UINT support = 0;
    return SUCCEEDED(d3d_device->CheckFormatSupport(dxgi_fmt, &support));
}

} // namespace d3d11
} // namespace nozzle
} // namespace bbb

#endif
