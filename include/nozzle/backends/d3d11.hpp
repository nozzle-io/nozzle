#pragma once

#if !defined(NOZZLE_HAS_D3D11)
#error "D3D11 backend not available on this platform"
#endif

#include <nozzle/types.hpp>
#include <nozzle/texture.hpp>
#include <nozzle/device.hpp>

struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11Texture2D;
#ifndef HANDLE
typedef void *HANDLE;
#endif

namespace nozzle::d3d11 {

struct DeviceDesc {
    ID3D11Device *device{nullptr};
    ID3D11DeviceContext *context{nullptr};
};

Result<device> wrap_device(const DeviceDesc &desc);

struct TextureWrapDesc {
    ID3D11Texture2D *texture{nullptr};
    // All three fields must match texture->GetDesc(); zero/UNKNOWN are not inferred.
    uint32_t dxgi_format{0};
    uint32_t width{0};
    uint32_t height{0};
};

// Rejects mismatched hints with InvalidArgument and unmapped native formats with
// UnsupportedFormat before retaining the texture or creating a shared handle.
// On success, owns an additional COM reference and the new NT shared handle;
// the caller keeps its original reference. CreateSharedHandle is one-shot per
// native resource, so a successfully wrapped resource cannot be wrapped again.
Result<texture> wrap_texture(const TextureWrapDesc &desc);

ID3D11Texture2D *get_texture(const texture &tex);
HANDLE get_shared_handle(const texture &tex);

} // namespace nozzle::d3d11
