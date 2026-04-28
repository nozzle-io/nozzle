// nozzle - d3d11_backend.cpp - Windows D3D11 backend (shared HANDLE textures)

#if NOZZLE_HAS_D3D11

#include <bbb/nozzle/backends/d3d11.hpp>
#include <bbb/nozzle/result.hpp>

namespace bbb {
namespace nozzle {
namespace d3d11 {

Result<device> wrap_device(const DeviceDesc &desc) {
	(void)desc;
	return Error{ErrorCode::BackendError, "D3D11 backend not yet implemented"};
}

Result<texture> wrap_texture(const TextureWrapDesc &desc) {
	(void)desc;
	return Error{ErrorCode::BackendError, "D3D11 backend not yet implemented"};
}

ID3D11Texture2D *get_texture(const texture &tex) {
	(void)tex;
	return nullptr;
}

HANDLE get_shared_handle(const texture &tex) {
	(void)tex;
	return nullptr;
}

} // namespace d3d11
} // namespace nozzle
} // namespace bbb

#endif
