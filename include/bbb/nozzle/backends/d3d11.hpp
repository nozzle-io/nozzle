#pragma once

#if !defined(NOZZLE_HAS_D3D11)
#error "D3D11 backend not available on this platform"
#endif

#include <bbb/nozzle/types.hpp>
#include <bbb/nozzle/texture.hpp>
#include <bbb/nozzle/device.hpp>

// Forward declare D3D11 types to avoid including d3d11.h in header
struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11Texture2D;
enum DXGI_FORMAT;
typedef void *HANDLE;

namespace bbb::nozzle::d3d11 {

struct DeviceDesc {
    ID3D11Device *device{nullptr};
    ID3D11DeviceContext *context{nullptr};
};

Result<device> wrap_device(const DeviceDesc &desc);

struct TextureWrapDesc {
    ID3D11Texture2D *texture{nullptr};
    uint32_t dxgi_format{0}; // DXGI_FORMAT as uint32_t
    uint32_t width{0};
    uint32_t height{0};
};

Result<texture> wrap_texture(const TextureWrapDesc &desc);

ID3D11Texture2D *get_texture(const texture &tex);
HANDLE get_shared_handle(const texture &tex);

} // namespace bbb::nozzle::d3d11
