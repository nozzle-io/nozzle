// nozzle - d3d11_texture.cpp - D3D11 texture creation and shared handle management

#if NOZZLE_HAS_D3D11

#include <bbb/nozzle/result.hpp>
#include <bbb/nozzle/texture.hpp>

namespace bbb {
namespace nozzle {
namespace d3d11 {

Result<texture> create_shared_texture(void *d3d11_device, uint32_t width, uint32_t height, uint32_t format) {
	(void)d3d11_device;
	(void)width;
	(void)height;
	(void)format;
	return Error{ErrorCode::BackendError, "D3D11 backend not yet implemented"};
}

Result<texture> lookup_shared_texture(void *d3d11_device, uint32_t shared_handle, uint32_t width, uint32_t height, uint32_t format) {
	(void)d3d11_device;
	(void)shared_handle;
	(void)width;
	(void)height;
	(void)format;
	return Error{ErrorCode::BackendError, "D3D11 backend not yet implemented"};
}

void release_d3d11_texture_resources(void *texture, void *shared_handle) {
	(void)texture;
	(void)shared_handle;
}

} // namespace d3d11
} // namespace nozzle
} // namespace bbb

#endif
