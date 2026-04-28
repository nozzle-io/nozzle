// nozzle - d3d11_sync.cpp - D3D11 keyed mutex synchronization

#if NOZZLE_HAS_D3D11

namespace bbb {
namespace nozzle {
namespace d3d11 {

void signal_slot_ready(void *shared_texture, uint32_t slot_index) {
	(void)shared_texture;
	(void)slot_index;
}

void signal_slot_done(void *shared_texture, uint32_t slot_index) {
	(void)shared_texture;
	(void)slot_index;
}

bool wait_for_slot(void *shared_texture, uint32_t slot_index, uint32_t timeout_ms) {
	(void)shared_texture;
	(void)slot_index;
	(void)timeout_ms;
	return false;
}

void release_slot(void *shared_texture, uint32_t slot_index) {
	(void)shared_texture;
	(void)slot_index;
}

} // namespace d3d11
} // namespace nozzle
} // namespace bbb

#endif
