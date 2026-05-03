// nozzle - sender.cpp - Texture sender with registry, ring buffer, and backend integration

#include <nozzle/sender.hpp>

#include <array>
#include <cstring>
#include <mutex>

#include <nozzle/device.hpp>
#include <nozzle/result.hpp>

#include <plog/Log.h>

#include "backends/backend_dispatch.hpp"
#include "frame_helpers.hpp"
#include "ipc.hpp"
#include "metadata.hpp"
#include "registry.hpp"
#include "shared_state.hpp"

namespace nozzle {

struct sender::Impl {
	detail::registry::Registration registration_{};
	detail::SenderSharedState *state{nullptr};
	detail::ipc::shm_handle state_handle{};
	device device_{};
	void *native_device_{nullptr};
	std::array<texture, detail::kMaxRingSlots> ring_textures_{};
	std::array<bool, detail::kMaxRingSlots> slot_in_use_{};
	uint32_t next_slot_{0};
	uint64_t frame_counter_{0};
	sender_info info_{};
	metadata_list metadata_{};
	bool allow_format_fallback_{true};
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

	uint8_t backend = static_cast<uint8_t>(detail::backend::get_backend_type());

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
	s.impl_->native_device_ = detail::backend::get_default_device();
	s.impl_->slot_in_use_.fill(false);
	s.impl_->info_.name = desc.name;
	s.impl_->info_.application_name = desc.application_name;
	s.impl_->info_.id = std::string(s.impl_->registration_.uuid);
	s.impl_->info_.backend = detail::backend::get_backend_type();
	s.impl_->metadata_ = desc.metadata;
	s.impl_->allow_format_fallback_ = desc.allow_format_fallback;
	s.impl_->valid_ = true;

	return s;
}

namespace {

texture_format get_next_fallback(texture_format fmt) {
    switch (fmt) {
        case texture_format::r8_unorm:      return texture_format::rg8_unorm;
        case texture_format::rg8_unorm:     return texture_format::rgba8_unorm;
        case texture_format::rgba8_unorm:   return texture_format::bgra8_unorm;
        case texture_format::bgra8_unorm:   return texture_format::unknown;
        case texture_format::r16_float:     return texture_format::rg16_float;
        case texture_format::rg16_float:    return texture_format::rgba16_float;
        case texture_format::rgba16_float:  return texture_format::rgba8_unorm;
        case texture_format::r32_float:     return texture_format::r16_float;
        case texture_format::rg32_float:    return texture_format::rg16_float;
        case texture_format::rgba32_float:  return texture_format::rgba16_float;
        case texture_format::r16_unorm:     return texture_format::rg16_unorm;
        case texture_format::rg16_unorm:    return texture_format::rgba16_unorm;
        case texture_format::rgba16_unorm:  return texture_format::rgba8_unorm;
        default:                            return texture_format::unknown;
    }
}

bool is_same_cpu_layout(texture_format a, texture_format b) {
    auto layout_group = [](texture_format f) -> int {
        switch (f) {
            case texture_format::r8_unorm: return 1;
            case texture_format::rg8_unorm: return 2;
            case texture_format::rgba8_unorm:
            case texture_format::bgra8_unorm: return 3;
            case texture_format::rgba8_srgb:
            case texture_format::bgra8_srgb: return 4;
            case texture_format::r16_unorm: return 5;
            case texture_format::r16_float: return 6;
            case texture_format::rg16_unorm: return 7;
            case texture_format::rg16_float: return 8;
            case texture_format::rgba16_unorm: return 9;
            case texture_format::rgba16_float: return 10;
            case texture_format::r32_float: return 11;
            case texture_format::rg32_float: return 12;
            case texture_format::rgba32_float: return 13;
            default: return 0;
        }
    };
    return layout_group(a) == layout_group(b) && layout_group(a) != 0;
}

} // namespace

Result<void> sender::publish_external_texture(const texture &tex) {
	if (!impl_ || !impl_->valid_) {
		return Error{ErrorCode::BackendError, "sender is not valid"};
	}

	if (!tex.valid()) {
		return Error{ErrorCode::InvalidArgument, "texture is not valid"};
	}

	std::lock_guard<std::mutex> lock(impl_->mutex_);

	uint64_t resource_id = detail::backend::get_shared_resource_id(tex);
	if (resource_id == 0) {
		return Error{ErrorCode::BackendError,
			"failed to get shared resource ID from texture"};
	}

	uint32_t ring_size = impl_->state->ring_size;
	if (ring_size < 1) {
		ring_size = 1;
	}

	uint32_t slot = impl_->next_slot_;
	impl_->next_slot_ = (impl_->next_slot_ + 1) % ring_size;
	uint64_t frame_number = ++impl_->frame_counter_;

	impl_->state->slots[slot].frame_number = frame_number;
	impl_->state->slots[slot].shared_resource_id = resource_id;

	const auto &tex_desc = tex.desc();
	impl_->state->channel_swizzle = static_cast<uint8_t>(tex_desc.swizzle);

	if (impl_->state->width != tex_desc.width ||
		impl_->state->height != tex_desc.height ||
		impl_->state->format != static_cast<uint32_t>(tex_desc.format)) {
		impl_->state->width = tex_desc.width;
		impl_->state->height = tex_desc.height;
		impl_->state->format = static_cast<uint32_t>(tex_desc.format);
	}

	detail::ipc::atomic_store_release_64(&impl_->state->committed_frame, frame_number);
	detail::ipc::atomic_store_release_32(&impl_->state->committed_slot, slot);

	return {};
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

	bool needs_create = !impl_->ring_textures_[slot].valid() ||
		impl_->ring_textures_[slot].desc().width != tdesc.width ||
		impl_->ring_textures_[slot].desc().height != tdesc.height ||
		!is_same_cpu_layout(impl_->ring_textures_[slot].desc().format, tdesc.format);

	if (needs_create) {
		if (!impl_->native_device_) {
			return Error{ErrorCode::BackendError,
				"device has no native handle"};
		}

		texture_format actual_format = tdesc.format;
		auto tex_result = detail::backend::create_ring_texture(
			impl_->native_device_,
			tdesc.width,
			tdesc.height,
			static_cast<uint32_t>(actual_format)
		);

		if (!tex_result.ok() && impl_->allow_format_fallback_) {
			texture_format fallback = get_next_fallback(actual_format);
			while (fallback != texture_format::unknown) {
				tex_result = detail::backend::create_ring_texture(
					impl_->native_device_,
					tdesc.width,
					tdesc.height,
					static_cast<uint32_t>(fallback)
				);
				if (tex_result.ok()) {
					LOG_WARNING << "format fallback: "
						<< static_cast<int>(tdesc.format)
						<< " -> " << static_cast<int>(fallback);
					actual_format = fallback;
					break;
				}
				fallback = get_next_fallback(fallback);
			}
		}

		if (!tex_result.ok()) {
			return tex_result.error();
		}

		impl_->ring_textures_[slot] = std::move(tex_result.value());

		texture_format observed_format = impl_->ring_textures_[slot].desc().format;
		texture_desc actual_desc{tdesc.width, tdesc.height, observed_format, tdesc.swizzle, tdesc.usage};
		impl_->slot_in_use_[slot] = true;

		impl_->state->width = tdesc.width;
		impl_->state->height = tdesc.height;
		impl_->state->format = static_cast<uint32_t>(observed_format);
		impl_->state->channel_swizzle = static_cast<uint8_t>(tdesc.swizzle);

		return detail::make_writable_frame(
			std::move(impl_->ring_textures_[slot]),
			actual_desc,
			slot
		);
	}

	impl_->slot_in_use_[slot] = true;

	texture_format actual_format = impl_->ring_textures_[slot].desc().format;

	impl_->state->width = tdesc.width;
	impl_->state->height = tdesc.height;
	impl_->state->format = static_cast<uint32_t>(actual_format);
	impl_->state->channel_swizzle = static_cast<uint8_t>(tdesc.swizzle);

	texture_desc actual_desc{tdesc.width, tdesc.height, actual_format, tdesc.swizzle, tdesc.usage};

	return detail::make_writable_frame(
		std::move(impl_->ring_textures_[slot]),
		actual_desc,
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

	uint64_t resource_id = detail::backend::get_shared_resource_id(f.get_texture());
	if (resource_id == 0) {
		f = writable_frame{};
		impl_->slot_in_use_[slot] = false;
		return Error{ErrorCode::BackendError,
			"frame texture has no shared resource"};
	}

	uint64_t frame_number = ++impl_->frame_counter_;

	impl_->state->slots[slot].frame_number = frame_number;
	impl_->state->slots[slot].shared_resource_id = resource_id;

	detail::ipc::atomic_store_release_64(&impl_->state->committed_frame, frame_number);
	detail::ipc::atomic_store_release_32(&impl_->state->committed_slot, slot);

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

Result<void> sender::publish_native_texture(void *native_texture, uint32_t width, uint32_t height, texture_format format) {
	if (!impl_ || !impl_->valid_) {
		return Error{ErrorCode::BackendError, "sender is not valid"};
	}
	if (!native_texture) {
		return Error{ErrorCode::InvalidArgument, "native_texture is null"};
	}
	if (width == 0 || height == 0) {
		return Error{ErrorCode::InvalidArgument, "dimensions must be non-zero"};
	}

	std::lock_guard<std::mutex> lock(impl_->mutex_);

	if (detail::backend::is_native_texture_shared(native_texture)) {
		void *surface = detail::backend::get_native_surface_from_texture(native_texture);
		if (surface) {
			auto wrapped = detail::backend::wrap_backend_texture(
				native_texture, surface, width, height, static_cast<uint32_t>(format));
			auto resource_id = detail::backend::get_shared_resource_id(wrapped);
			if (resource_id != 0) {
				uint32_t ring_size = impl_->state->ring_size;
				if (ring_size < 1) ring_size = 1;
				uint32_t slot = impl_->next_slot_;
				impl_->next_slot_ = (impl_->next_slot_ + 1) % ring_size;
				uint64_t frame_number = ++impl_->frame_counter_;
				impl_->state->slots[slot].frame_number = frame_number;
				impl_->state->slots[slot].shared_resource_id = resource_id;
				impl_->state->width = width;
				impl_->state->height = height;
				impl_->state->format = static_cast<uint32_t>(wrapped.desc().format);
				impl_->state->channel_swizzle = static_cast<uint8_t>(channel_swizzle::identity);
				detail::ipc::atomic_store_release_64(&impl_->state->committed_frame, frame_number);
				detail::ipc::atomic_store_release_32(&impl_->state->committed_slot, slot);
				return {};
			}
		}
	}

	texture_desc td{};
	td.width = width;
	td.height = height;
	td.format = format;

	auto frame_result = detail::make_writable_frame(
		texture{}, td, 0);

	{
		texture_desc tdesc{width, height, format};
		bool needs_create = true;

		uint32_t ring_size = impl_->state->ring_size;
		if (ring_size < 1) ring_size = 1;
		int32_t free_slot = -1;
		for (uint32_t i = 0; i < ring_size; ++i) {
			if (!impl_->slot_in_use_[i]) {
				free_slot = static_cast<int32_t>(i);
				break;
			}
		}
		if (free_slot < 0) {
			return Error{ErrorCode::Timeout, "all ring buffer slots are in use"};
		}
		uint32_t slot = static_cast<uint32_t>(free_slot);

		needs_create = !impl_->ring_textures_[slot].valid() ||
			impl_->ring_textures_[slot].desc().width != width ||
			impl_->ring_textures_[slot].desc().height != height ||
			impl_->ring_textures_[slot].desc().format != format;

		if (needs_create) {
			auto tex_result = detail::backend::create_ring_texture(
				impl_->native_device_, width, height, static_cast<uint32_t>(format));
			if (!tex_result.ok()) return tex_result.error();
			impl_->ring_textures_[slot] = std::move(tex_result.value());
		}

		void *dst_native = detail::backend::get_native_texture(impl_->ring_textures_[slot]);
		if (!dst_native) {
			return Error{ErrorCode::BackendError, "no native handle on ring texture"};
		}

		auto blit_result = detail::backend::blit_textures(
			impl_->native_device_, native_texture, dst_native, width, height);
		if (!blit_result.ok()) return blit_result.error();

		impl_->slot_in_use_[slot] = true;
		texture_format actual_fmt = impl_->ring_textures_[slot].desc().format;
		impl_->state->width = width;
		impl_->state->height = height;
		impl_->state->format = static_cast<uint32_t>(actual_fmt);
		impl_->state->channel_swizzle = static_cast<uint8_t>(channel_swizzle::identity);

		uint64_t resource_id = detail::backend::get_shared_resource_id(impl_->ring_textures_[slot]);
		if (resource_id == 0) {
			impl_->slot_in_use_[slot] = false;
			return Error{ErrorCode::BackendError, "ring texture has no shared resource"};
		}

		uint64_t frame_number = ++impl_->frame_counter_;
		impl_->state->slots[slot].frame_number = frame_number;
		impl_->state->slots[slot].shared_resource_id = resource_id;
		detail::ipc::atomic_store_release_64(&impl_->state->committed_frame, frame_number);
		detail::ipc::atomic_store_release_32(&impl_->state->committed_slot, slot);
		impl_->slot_in_use_[slot] = false;
	}

	return {};
}

} // namespace nozzle
