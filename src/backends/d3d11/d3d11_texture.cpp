// nozzle - d3d11_texture.cpp - D3D11 texture creation and shared handle management

#if NOZZLE_HAS_D3D11

#include <nozzle/result.hpp>
#include <nozzle/texture.hpp>

#include "common/shared_state.hpp"

#include <d3d11.h>
#include <dxgi1_2.h>

namespace nozzle {
namespace d3d11 {

static DXGI_FORMAT to_dxgi_format(uint32_t nozzle_format) {
    switch (static_cast<texture_format>(nozzle_format)) {
        case texture_format::r8_unorm:         return DXGI_FORMAT_R8_UNORM;
        case texture_format::rg8_unorm:        return DXGI_FORMAT_R8G8_UNORM;
        case texture_format::rgba8_unorm:      return DXGI_FORMAT_R8G8B8A8_UNORM;
        case texture_format::bgra8_unorm:      return DXGI_FORMAT_B8G8R8A8_UNORM;
        case texture_format::rgba8_srgb:       return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        case texture_format::bgra8_srgb:       return DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
        case texture_format::r16_unorm:        return DXGI_FORMAT_R16_UNORM;
        case texture_format::rg16_unorm:       return DXGI_FORMAT_R16G16_UNORM;
        case texture_format::rgba16_unorm:     return DXGI_FORMAT_R16G16B16A16_UNORM;
        case texture_format::r16_float:        return DXGI_FORMAT_R16_FLOAT;
        case texture_format::rg16_float:       return DXGI_FORMAT_R16G16_FLOAT;
        case texture_format::rgba16_float:     return DXGI_FORMAT_R16G16B16A16_FLOAT;
        case texture_format::r32_float:        return DXGI_FORMAT_R32_FLOAT;
        case texture_format::rg32_float:       return DXGI_FORMAT_R32G32_FLOAT;
        case texture_format::rgba32_float:     return DXGI_FORMAT_R32G32B32A32_FLOAT;
        case texture_format::r32_uint:         return DXGI_FORMAT_R32_UINT;
        case texture_format::rgba32_uint:      return DXGI_FORMAT_R32G32B32A32_UINT;
        case texture_format::depth32_float:    return DXGI_FORMAT_D32_FLOAT;
        default:                               return DXGI_FORMAT_UNKNOWN;
    }
}

Result<texture> create_shared_texture(
    void *d3d11_device,
    uint32_t width,
    uint32_t height,
    uint32_t format
) {
    if (!d3d11_device) {
        return Error{ErrorCode::InvalidArgument, "D3D11 device is null"};
    }
    if (width == 0 || height == 0) {
        return Error{ErrorCode::InvalidArgument, "Texture dimensions must be non-zero"};
    }

    auto *device = static_cast<ID3D11Device *>(d3d11_device);

    DXGI_FORMAT dxgi_fmt = to_dxgi_format(format);
    if (dxgi_fmt == DXGI_FORMAT_UNKNOWN) {
        return Error{ErrorCode::UnsupportedFormat, "Unsupported nozzle pixel format for D3D11"};
    }

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = dxgi_fmt;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;

    ID3D11Texture2D *texture = nullptr;
    HRESULT hr = device->CreateTexture2D(&desc, nullptr, &texture);
    if (FAILED(hr) || !texture) {
        return Error{ErrorCode::ResourceCreationFailed, "Failed to create D3D11 shared texture"};
    }

    IDXGIResource *dxgi_resource = nullptr;
    hr = texture->QueryInterface(
        __uuidof(IDXGIResource), reinterpret_cast<void **>(&dxgi_resource));
    if (FAILED(hr) || !dxgi_resource) {
        texture->Release();
        return Error{ErrorCode::BackendError, "Texture does not support IDXGIResource"};
    }

    HANDLE shared_handle = nullptr;
    hr = dxgi_resource->GetSharedHandle(&shared_handle);
    dxgi_resource->Release();

    if (FAILED(hr) || !shared_handle) {
        texture->Release();
        return Error{ErrorCode::SharedHandleFailed, "Failed to get shared handle"};
    }

    native_format_desc native{};
    native.backend = backend_type::d3d11;
    native.kind = native_format_kind::dxgi_format;
    native.value = static_cast<uint32_t>(dxgi_fmt);

    return detail::make_texture_from_backend(
        reinterpret_cast<void *>(texture),
        reinterpret_cast<void *>(shared_handle),
        width,
        height,
        format,
        0,
        &native
    );
}

Result<texture> lookup_shared_texture(
    void *d3d11_device,
    uint64_t shared_handle_val,
    uint32_t width,
    uint32_t height,
    uint32_t format
) {
    if (!d3d11_device) {
        return Error{ErrorCode::InvalidArgument, "D3D11 device is null"};
    }
    if (shared_handle_val == detail::kInvalidSharedResourceId) {
        return Error{ErrorCode::InvalidArgument, "shared handle is zero"};
    }

    auto *device = static_cast<ID3D11Device *>(d3d11_device);
    HANDLE shared_handle = reinterpret_cast<HANDLE>(shared_handle_val);

    ID3D11Texture2D *texture = nullptr;
    HRESULT hr = device->OpenSharedResource(
        shared_handle, __uuidof(ID3D11Texture2D),
        reinterpret_cast<void **>(&texture));
    if (FAILED(hr) || !texture) {
        return Error{ErrorCode::ResourceCreationFailed,
            "Failed to open shared D3D11 texture"};
    }

    D3D11_TEXTURE2D_DESC tex_desc{};
    texture->GetDesc(&tex_desc);

    native_format_desc native{};
    native.backend = backend_type::d3d11;
    native.kind = native_format_kind::dxgi_format;
    native.value = static_cast<uint32_t>(tex_desc.Format);

    return detail::make_texture_from_backend(
        reinterpret_cast<void *>(texture),
        reinterpret_cast<void *>(shared_handle),
        width,
        height,
        format,
        0,
        &native
    );
}

void release_d3d11_texture_resources(void *texture_ptr, void *shared_handle_ptr) {
    if (texture_ptr) {
        static_cast<ID3D11Texture2D *>(texture_ptr)->Release();
    }
    if (shared_handle_ptr) {
        CloseHandle(reinterpret_cast<HANDLE>(shared_handle_ptr));
    }
}

Result<void> blit_to_texture(void *src_texture_ptr, void *dst_texture_ptr) {
    if (!src_texture_ptr || !dst_texture_ptr) {
        return Error{ErrorCode::InvalidArgument, "blit_to_texture: null texture"};
    }

    auto *src = static_cast<ID3D11Texture2D *>(src_texture_ptr);
    auto *dst = static_cast<ID3D11Texture2D *>(dst_texture_ptr);

    ID3D11Device *device = nullptr;
    src->GetDevice(&device);
    if (!device) {
        return Error{ErrorCode::BackendError, "failed to get D3D11 device from source texture"};
    }

    ID3D11DeviceContext *ctx = nullptr;
    device->GetImmediateContext(&ctx);
    device->Release();

    if (!ctx) {
        return Error{ErrorCode::BackendError, "failed to get D3D11 device context"};
    }

    ctx->CopySubresourceRegion(dst, 0, 0, 0, 0, src, 0, nullptr);
    ctx->Release();

    return {};
}

Result<void> blit_from_texture(void *src_texture_ptr, void *dst_texture_ptr) {
    return blit_to_texture(src_texture_ptr, dst_texture_ptr);
}

} // namespace d3d11
} // namespace nozzle

#endif
