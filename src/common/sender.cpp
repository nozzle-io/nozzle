// nozzle - sender.cpp - Texture sender with registry, ring buffer, and backend integration

#include <bbb/nozzle/sender.hpp>

#include <array>
#include <cstring>
#include <mutex>

#include <bbb/nozzle/device.hpp>
#include <bbb/nozzle/result.hpp>

#include "frame_helpers.hpp"
#include "ipc.hpp"
#include "metadata.hpp"
#include "registry.hpp"
#include "shared_state.hpp"

#if NOZZLE_HAS_METAL
#include "backends/metal/metal_helpers.hpp"
namespace bbb::nozzle::metal {
	void *get_default_mtl_device();
} // namespace bbb::nozzle::metal
#endif

namespace bbb::nozzle {

struct sender::Impl {
	detail::registry::Registration registration_{};
	detail::SenderSharedState *state{nullptr};
	detail::ipc::shm_handle state_handle{};
	device device_{};
#if NOZZLE_HAS_METAL
	void *native_device_{nullptr};
#endif
	std::array<texture, detail::kMaxRingSlots> ring_textures_{};
	std::array<bool, detail::kMaxRingSlots> slot_in_use_{};
	uint32_t next_slot_{0};
	uint64_t frame_counter_{0};
	sender_info info_{};
	metadata_list metadata_{};
	bool valid_{false};
	std::mutex mutex_;

	~Impl() {
		valid_ = false;
		for (uint32_t i = 0; i < detail::kMaxRingSlots; ++i) {
			slot_in_use_[i] = false;
		}
		if (state != nullptr) {
			detail::ipc::shm_unmap(state, sizeof(detail::SenderSharedState));
			state = nullptr;
		}
		detail::ipc::shm_close(state_handle);
		detail::registry::unregister_sender(registration_.uuid);
	}
};

sender::sender() = default;
sender::~sender() = default;
sender::sender(sender &&) noexcept = default;
sender &sender::operator=(sender &&) noexcept = default;

Result<sender> sender::create(const sender_desc &desc) {
	if (desc.name.empty()) {
		return Error{ErrorCode::InvalidArgument, "sender name must not be empty"};
	}

	const char *app_name = desc.application_name.empty()
		? "unknown"
		: desc.application_name.c_str();

	auto dev_result = device::default_device();
	if (!dev_result.ok()) {
		return dev_result.error();
	}
	auto dev = std::move(dev_result.value());

	uint8_t backend = 0;
#if NOZZLE_HAS_METAL
	backend = static_cast<uint8_t>(backend_type::metal);
#endif

	auto reg_result = detail::registry::register_sender(
		desc.name.c_str(),
		app_name,
		backend,
		0, 0, 0,
		desc.ring_buffer_size
	);
	if (!reg_result.ok()) {
		return reg_result.error();
	}
	auto reg = std::move(reg_result.value());

	auto state_shm = detail::ipc::shm_open_rw(reg.shm_name);
	if (!state_shm.ok()) {
		detail::registry::unregister_sender(reg.uuid);
		return Error{ErrorCode::ResourceCreationFailed,
			"failed to reopen sender shared state"};
	}

	auto mapped = detail::ipc::shm_map(state_shm.value(), sizeof(detail::SenderSharedState), false);
	if (!mapped.ok()) {
		detail::ipc::shm_close(state_shm.value());
		detail::registry::unregister_sender(reg.uuid);
		return Error{ErrorCode::ResourceCreationFailed,
			"failed to mmap sender shared state"};
	}

	auto *state = static_cast<detail::SenderSharedState *>(mapped.value());
	if (state->magic != detail::kSenderMagic) {
		detail::ipc::shm_unmap(mapped.value(), sizeof(detail::SenderSharedState));
		detail::ipc::shm_close(state_shm.value());
		detail::registry::unregister_sender(reg.uuid);
		return Error{ErrorCode::BackendError,
			"sender state has invalid magic"};
	}

	sender s;
	s.impl_ = std::make_unique<Impl>();
	s.impl_->registration_ = std::move(reg);
	s.impl_->state = state;
	s.impl_->state_handle = std::move(state_shm.value());
	s.impl_->device_ = std::move(dev);
#if NOZZLE_HAS_METAL
	s.impl_->native_device_ = metal::get_default_mtl_device();
#endif
	s.impl_->slot_in_use_.fill(false);
	s.impl_->info_.name = desc.name;
	s.impl_->info_.application_name = desc.application_name;
	s.impl_->info_.id = std::string(s.impl_->registration_.uuid);
#if NOZZLE_HAS_METAL
	s.impl_->info_.backend = backend_type::metal;
#endif
	s.impl_->metadata_ = desc.metadata;
	s.impl_->valid_ = true;

	return s;
}

Result<void> sender::publish_external_texture(const texture &tex) {
	if (!impl_ || !impl_->valid_) {
		return Error{ErrorCode::BackendError, "sender is not valid"};
	}

	if (!tex.valid()) {
		return Error{ErrorCode::InvalidArgument, "texture is not valid"};
	}

#if NOZZLE_HAS_METAL
	std::lock_guard<std::mutex> lock(impl_->mutex_);

	void *surface = detail::get_surface_native(tex);
	if (!surface) {
		return Error{ErrorCode::InvalidArgument,
			"texture has no native surface"};
	}

	uint32_t surface_id = metal::get_iosurface_id(surface);
	if (surface_id == 0) {
		return Error{ErrorCode::BackendError,
			"failed to get IOSurface ID"};
	}

	uint32_t ring_size = impl_->state->ring_size;
	if (ring_size < 1) {
		ring_size = 1;
	}

	uint32_t slot = impl_->next_slot_;
	impl_->next_slot_ = (impl_->next_slot_ + 1) % ring_size;
	uint64_t frame_number = ++impl_->frame_counter_;

	impl_->state->slots[slot].frame_number = frame_number;
	impl_->state->slots[slot].iosurface_id = surface_id;

	const auto &tex_desc = tex.desc();
	if (impl_->state->width != tex_desc.width ||
		impl_->state->height != tex_desc.height) {
		impl_->state->width = tex_desc.width;
		impl_->state->height = tex_desc.height;
	}

	detail::ipc::atomic_store_release_64(&impl_->state->committed_frame, frame_number);
	detail::ipc::atomic_store_release_32(&impl_->state->committed_slot, slot);

	return {};
#else
	(void)tex;
	return Error{ErrorCode::UnsupportedBackend, "no backend available"};
#endif
}

Result<writable_frame> sender::acquire_writable_frame(const texture_desc &tdesc) {
	if (!impl_ || !impl_->valid_) {
		return Error{ErrorCode::BackendError, "sender is not valid"};
	}

	if (tdesc.width == 0 || tdesc.height == 0) {
		return Error{ErrorCode::InvalidArgument,
			"texture dimensions must be non-zero"};
	}

	std::lock_guard<std::mutex> lock(impl_->mutex_);

	uint32_t ring_size = impl_->state->ring_size;
	if (ring_size < 1) {
		ring_size = 1;
	}

	int32_t free_slot = -1;
	for (uint32_t i = 0; i < ring_size; ++i) {
		if (!impl_->slot_in_use_[i]) {
			free_slot = static_cast<int32_t>(i);
			break;
		}
	}
	if (free_slot < 0) {
		return Error{ErrorCode::Timeout,
			"all ring buffer slots are in use"};
	}

	uint32_t slot = static_cast<uint32_t>(free_slot);

#if NOZZLE_HAS_METAL
	bool needs_create = !impl_->ring_textures_[slot].valid() ||
		impl_->ring_textures_[slot].desc().width != tdesc.width ||
		impl_->ring_textures_[slot].desc().height != tdesc.height ||
		impl_->ring_textures_[slot].desc().format != tdesc.format;

	if (needs_create) {
		if (!impl_->native_device_) {
			return Error{ErrorCode::BackendError,
				"device has no native handle"};
		}

		auto pair_result = metal::create_iosurface_texture(
			impl_->native_device_,
			tdesc.width,
			tdesc.height,
			static_cast<uint32_t>(tdesc.format)
		);
		if (!pair_result.ok()) {
			return pair_result.error();
		}
		auto &pair = pair_result.value();

		impl_->ring_textures_[slot] = detail::make_texture_from_backend(
			pair.mtl_texture,
			pair.io_surface,
			pair.width,
			pair.height,
			pair.pixel_format
		);
	}
#endif

	impl_->slot_in_use_[slot] = true;

	impl_->state->width = tdesc.width;
	impl_->state->height = tdesc.height;
	impl_->state->format = static_cast<uint32_t>(tdesc.format);

	return detail::make_writable_frame(
		std::move(impl_->ring_textures_[slot]),
		tdesc,
		slot
	);
}

Result<void> sender::commit_frame(writable_frame &f) {
	if (!impl_ || !impl_->valid_) {
		return Error{ErrorCode::BackendError, "sender is not valid"};
	}

	if (!f.valid()) {
		return Error{ErrorCode::InvalidArgument, "frame is not valid"};
	}

	std::lock_guard<std::mutex> lock(impl_->mutex_);

	uint32_t slot = detail::get_writable_frame_slot(f);
	uint32_t ring_size = impl_->state->ring_size;
	if (ring_size < 1) {
		ring_size = 1;
	}
	if (slot >= ring_size) {
		return Error{ErrorCode::InvalidArgument,
			"frame slot index out of range"};
	}

#if NOZZLE_HAS_METAL
	void *surface = detail::get_surface_native(f.get_texture());
	if (!surface) {
		f = writable_frame{};
		impl_->slot_in_use_[slot] = false;
		return Error{ErrorCode::BackendError,
			"frame texture has no native surface"};
	}

	uint32_t surface_id = metal::get_iosurface_id(surface);
	uint64_t frame_number = ++impl_->frame_counter_;

	impl_->state->slots[slot].frame_number = frame_number;
	impl_->state->slots[slot].iosurface_id = surface_id;

	detail::ipc::atomic_store_release_64(&impl_->state->committed_frame, frame_number);
	detail::ipc::atomic_store_release_32(&impl_->state->committed_slot, slot);
#endif

	if (f.valid()) {
		impl_->ring_textures_[slot] = std::move(f.get_texture());
	}
	f = writable_frame{};
	impl_->slot_in_use_[slot] = false;

	return {};
}

sender_info sender::info() const {
	if (!impl_) {
		return {};
	}
	return impl_->info_;
}

Result<void> sender::set_metadata(const metadata_list &metadata) {
	if (!impl_ || !impl_->valid_) {
		return Error{ErrorCode::BackendError, "sender is not valid"};
	}

	std::lock_guard<std::mutex> lock(impl_->mutex_);

	impl_->metadata_ = metadata;
	detail::serialize_metadata(
		metadata,
		impl_->state->metadata,
		sizeof(impl_->state->metadata));

	return {};
}

bool sender::valid() const {
	return impl_ && impl_->valid_;
}

} // namespace bbb::nozzle
