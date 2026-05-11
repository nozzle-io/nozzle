// nozzle - sender.cpp - Texture sender with registry, ring buffer, and backend integration

#include <nozzle/sender.hpp>

#include <array>
#include <cstring>
#include <exception>
#include <mutex>

#include <nozzle/config.hpp>
#include <nozzle/device.hpp>
#include <nozzle/format_resolve.hpp>
#include <nozzle/result.hpp>

#include <plog/Log.h>

#include "backends/backend_dispatch.hpp"
#include "frame_helpers.hpp"
#include "ipc.hpp"
#include "metadata.hpp"
#include "registry.hpp"
#include "sender_detail.hpp"
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
	uint32_t fallback_flags_{fallback_safe_defaults};
	bool owns_socket_{false};
	bool valid_{false};
	std::mutex mutex_;

	~Impl() {
		valid_ = false;
		for (uint32_t i = 0; i < detail::kMaxRingSlots; ++i) {
			slot_in_use_[i] = false;
		}
		if (owns_socket_) {
			detail::backend::cleanup_sender_socket();
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

	auto flag_result = validate_fallback_flags(desc.fallback_flags);
	if (!flag_result.ok()) {
		return flag_result.error();
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
#if NOZZLE_HAS_EXCEPTIONS
	try {
#endif
		s.impl_ = std::make_unique<Impl>();
#if NOZZLE_HAS_EXCEPTIONS
	} catch (const std::exception &) {
		detail::ipc::shm_unmap(state, sizeof(detail::SenderSharedState));
		detail::ipc::shm_close(state_shm.value());
		detail::registry::unregister_sender(reg.uuid);
		return Error{ErrorCode::ResourceCreationFailed,
			"sender allocation failed"};
	}
#endif
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
	s.impl_->fallback_flags_ = desc.fallback_flags;
	s.impl_->valid_ = true;

	auto notify_result = detail::backend::notify_sender_uuid(reg.uuid);
	if (!notify_result.ok()) {
		return notify_result.error();
	}
	s.impl_->owns_socket_ = true;

	return s;
}

namespace {

Result<void> apply_format_metadata(detail::SenderSharedState *state,
    texture_format requested, texture_format actual,
    fallback_category category, texture_format fallback_target) {
    auto meta = resolve_fallback_metadata(requested, actual, category, fallback_target);
    if (!meta.ok()) {
        return meta.error();
    }
    state->format = static_cast<uint32_t>(meta.value().storage_format);
    state->semantic_format = static_cast<uint32_t>(meta.value().semantic_format);
    state->channel_swizzle = static_cast<uint8_t>(meta.value().swizzle);
    return {};
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
	if (resource_id == detail::kInvalidSharedResourceId) {
		return Error{ErrorCode::BackendError,
			"failed to get shared resource ID from texture"};
	}

	uint32_t ring_size = impl_->state->ring_size;
	if (ring_size < detail::kMinimumRingSize) {
		ring_size = detail::kMinimumRingSize;
	}

	uint32_t slot = impl_->next_slot_;
	impl_->next_slot_ = (impl_->next_slot_ + 1) % ring_size;
	uint64_t frame_number = ++impl_->frame_counter_;

	impl_->state->slots[slot].frame_number = frame_number;
	impl_->state->slots[slot].shared_resource_id = resource_id;

	const auto &tex_desc = tex.desc();
	impl_->state->slots[slot].width = tex_desc.width;
	impl_->state->slots[slot].height = tex_desc.height;
	impl_->state->slots[slot].format = static_cast<uint32_t>(tex_desc.format);
	impl_->state->slots[slot].semantic_format = static_cast<uint32_t>(tex_desc.semantic_format);
	impl_->state->slots[slot].channel_swizzle = static_cast<uint8_t>(tex_desc.swizzle);

	const auto &resolved = tex.resolved();
	impl_->state->slots[slot].native_format_kind = static_cast<uint8_t>(resolved.native.kind);
	impl_->state->slots[slot].format_source = static_cast<uint8_t>(resolved.source);
	impl_->state->slots[slot].native_format_value = resolved.native.value;
	impl_->state->slots[slot].native_format_modifier = resolved.native.modifier;
	impl_->state->slots[slot].plane_count = resolved.native.plane_count;
	for (uint32_t i = 0; i < resolved.native.plane_count && i < 4; ++i) {
		impl_->state->slots[slot].plane_strides[i] = resolved.native.plane_strides[i];
		impl_->state->slots[slot].plane_offsets[i] = resolved.native.plane_offsets[i];
	}

	impl_->state->semantic_format = static_cast<uint32_t>(tex_desc.semantic_format);
	if (		impl_->state->width != tex_desc.width ||
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
	if (ring_size < detail::kMinimumRingSize) {
		ring_size = detail::kMinimumRingSize;
	}

	int32_t free_slot = detail::kSlotNotFound;
	for (uint32_t i = 0; i < ring_size; ++i) {
		if (!impl_->slot_in_use_[i]) {
			free_slot = static_cast<int32_t>(i);
			break;
		}
	}
	if (free_slot == detail::kSlotNotFound) {
		return Error{ErrorCode::Timeout,
			"all ring buffer slots are in use"};
	}

	uint32_t slot = static_cast<uint32_t>(free_slot);

	bool needs_create = !impl_->ring_textures_[slot].valid() ||
		impl_->ring_textures_[slot].desc().width != tdesc.width ||
		impl_->ring_textures_[slot].desc().height != tdesc.height;

	if (!needs_create) {
		auto match = classify_reuse(
			impl_->ring_textures_[slot].desc().format,
			tdesc.format,
			impl_->fallback_flags_);
		needs_create = (match == reuse_match::disallowed);
	}

	if (needs_create) {
		if (!impl_->native_device_) {
			return Error{ErrorCode::BackendError,
				"device has no native handle"};
		}

		texture_format attempt_format = tdesc.format;
		texture_format fallback_target{texture_format::unknown};
		fallback_category attempted_category{fallback_category::none};
		auto tex_result = detail::backend::create_ring_texture(
			impl_->native_device_,
			tdesc.width,
			tdesc.height,
			static_cast<uint32_t>(attempt_format),
			slot
		);

		if (!tex_result.ok()) {
			auto fb = resolve_fallback(attempt_format, impl_->fallback_flags_);
			if (fb.valid) {
				fallback_target = fb.target;
				attempted_category = fb.category;
				tex_result = detail::backend::create_ring_texture(
					impl_->native_device_,
					tdesc.width,
					tdesc.height,
					static_cast<uint32_t>(fb.target),
					slot
				);
				if (tex_result.ok()) {
					LOG_WARNING << "format fallback ["
						<< fallback_category_name(fb.category) << "]: "
						<< static_cast<int>(attempt_format)
						<< " -> " << static_cast<int>(fb.target);
				}
			}
		}

		if (!tex_result.ok()) {
			return tex_result.error();
		}

		impl_->ring_textures_[slot] = std::move(tex_result.value());

		texture_format observed_format = impl_->ring_textures_[slot].desc().format;

		auto category_result = classify_observed_format(
			tdesc.format, observed_format,
			attempted_category, fallback_target,
			impl_->fallback_flags_);
		if (!category_result.ok()) {
			impl_->ring_textures_[slot] = texture{};
			return category_result.error();
		}
		fallback_category final_category = category_result.value();

		texture_desc actual_desc{tdesc.width, tdesc.height, observed_format, tdesc.semantic_format, tdesc.swizzle, tdesc.usage};
		impl_->slot_in_use_[slot] = true;

		impl_->state->width = tdesc.width;
		impl_->state->height = tdesc.height;
		auto meta_result = apply_format_metadata(impl_->state, tdesc.format, observed_format, final_category, fallback_target);
		if (!meta_result.ok()) {
			return meta_result.error();
		}

		return detail::make_writable_frame(
			std::move(impl_->ring_textures_[slot]),
			actual_desc,
			slot
		);
	}

	impl_->slot_in_use_[slot] = true;

	texture_format reused_format = impl_->ring_textures_[slot].desc().format;

	impl_->state->width = tdesc.width;
	impl_->state->height = tdesc.height;

	auto category_result = classify_observed_format(
		tdesc.format, reused_format,
		fallback_category::none, texture_format::unknown,
		impl_->fallback_flags_);
	if (!category_result.ok()) {
		return category_result.error();
	}
	fallback_category reuse_category = category_result.value();

	auto meta_result = apply_format_metadata(impl_->state, tdesc.format, reused_format, reuse_category, texture_format::unknown);
	if (!meta_result.ok()) {
		return meta_result.error();
	}

	texture_desc actual_desc{tdesc.width, tdesc.height, reused_format, tdesc.semantic_format, tdesc.swizzle, tdesc.usage};

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
	if (ring_size < detail::kMinimumRingSize) {
		ring_size = detail::kMinimumRingSize;
	}
	if (slot >= ring_size) {
		return Error{ErrorCode::InvalidArgument,
			"frame slot index out of range"};
	}

	uint64_t resource_id = detail::backend::get_shared_resource_id(f.get_texture());
	if (resource_id == detail::kInvalidSharedResourceId) {
		f = writable_frame{};
		impl_->slot_in_use_[slot] = false;
		return Error{ErrorCode::BackendError,
			"frame texture has no shared resource"};
	}

	uint64_t frame_number = ++impl_->frame_counter_;

	impl_->state->slots[slot].frame_number = frame_number;
	impl_->state->slots[slot].shared_resource_id = resource_id;
	impl_->state->slots[slot].width = impl_->state->width;
	impl_->state->slots[slot].height = impl_->state->height;
	impl_->state->slots[slot].format = impl_->state->format;
	impl_->state->slots[slot].semantic_format = impl_->state->semantic_format;
	impl_->state->slots[slot].channel_swizzle = impl_->state->channel_swizzle;

	if (impl_->ring_textures_[slot].valid()) {
		const auto &resolved = impl_->ring_textures_[slot].resolved();
		impl_->state->slots[slot].native_format_kind = static_cast<uint8_t>(resolved.native.kind);
		impl_->state->slots[slot].format_source = static_cast<uint8_t>(resolved.source);
		impl_->state->slots[slot].native_format_value = resolved.native.value;
		impl_->state->slots[slot].native_format_modifier = resolved.native.modifier;
		impl_->state->slots[slot].plane_count = resolved.native.plane_count;
		for (uint32_t i = 0; i < resolved.native.plane_count && i < 4; ++i) {
			impl_->state->slots[slot].plane_strides[i] = resolved.native.plane_strides[i];
			impl_->state->slots[slot].plane_offsets[i] = resolved.native.plane_offsets[i];
		}
	}

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
	return publish_native_texture(native_texture, width, height, format, format);
}

Result<void> sender::publish_native_texture(void *native_texture, uint32_t width, uint32_t height, texture_format storage_format, texture_format semantic_format) {
	if (!impl_ || !impl_->valid_) {
		return Error{ErrorCode::BackendError, "sender is not valid"};
	}
	if (!native_texture) {
		return Error{ErrorCode::InvalidArgument, "native_texture is null"};
	}
	if (width == 0 || height == 0) {
		return Error{ErrorCode::InvalidArgument, "dimensions must be non-zero"};
	}
	if (storage_format != semantic_format) {
		return Error{ErrorCode::UnsupportedFormat,
			"publish_native_texture does not support storage/semantic format mismatch"};
	}

	std::lock_guard<std::mutex> lock(impl_->mutex_);

	if (detail::backend::is_native_texture_shared(native_texture)) {
		void *surface = detail::backend::get_native_surface_from_texture(native_texture);
		if (surface) {
			auto resource_id = detail::backend::get_shared_resource_id_from_surface(surface);
			if (resource_id != detail::kInvalidSharedResourceId) {
				if (detail::backend::is_surface_globally_shared(surface)) {
					resolved_texture_format resolved{};
					resolved.semantic_format = storage_format;
					resolved.storage_format = storage_format;
					resolved.cpu_layout = resolve_cpu_layout(storage_format);
					resolved.sampling = resolve_sampling(storage_format);
					resolved.native.value = static_cast<uint32_t>(storage_format);
					resolved.source = format_source::requested;
					resolved.swizzle = channel_swizzle::identity;

					uint32_t ring_size = impl_->state->ring_size;
					if (ring_size < detail::kMinimumRingSize) ring_size = detail::kMinimumRingSize;
					uint32_t slot = impl_->next_slot_;
					impl_->next_slot_ = (impl_->next_slot_ + 1) % ring_size;
					uint64_t frame_number = ++impl_->frame_counter_;
					detail::write_slot_metadata(impl_->state->slots[slot],
						frame_number, resource_id, width, height,
						storage_format, semantic_format,
						channel_swizzle::identity, resolved);
					detail::write_global_metadata(*impl_->state,
						width, height, storage_format, semantic_format,
						channel_swizzle::identity);
					detail::ipc::atomic_store_release_64(&impl_->state->committed_frame, frame_number);
					detail::ipc::atomic_store_release_32(&impl_->state->committed_slot, slot);

					detail::backend::release_texture_resources(nullptr, surface);
					return {};
				}
			}
			detail::backend::release_texture_resources(nullptr, surface);
		}
	}

	{
		uint32_t ring_size = impl_->state->ring_size;
		if (ring_size < detail::kMinimumRingSize) ring_size = detail::kMinimumRingSize;
		int32_t free_slot = detail::kSlotNotFound;
		for (uint32_t i = 0; i < ring_size; ++i) {
			if (!impl_->slot_in_use_[i]) {
				free_slot = static_cast<int32_t>(i);
				break;
			}
		}
		if (free_slot == detail::kSlotNotFound) {
			return Error{ErrorCode::Timeout, "all ring buffer slots are in use"};
		}
		uint32_t slot = static_cast<uint32_t>(free_slot);

		bool needs_create = !impl_->ring_textures_[slot].valid() ||
			impl_->ring_textures_[slot].desc().width != width ||
			impl_->ring_textures_[slot].desc().height != height ||
			impl_->ring_textures_[slot].desc().format != storage_format;

		if (needs_create) {
			auto tex_result = detail::backend::create_ring_texture(
				impl_->native_device_, width, height, static_cast<uint32_t>(storage_format), slot);
			if (!tex_result.ok()) return tex_result.error();
			impl_->ring_textures_[slot] = std::move(tex_result.value());
		}

		texture_format ring_actual = impl_->ring_textures_[slot].desc().format;
		if (ring_actual != storage_format) {
			impl_->ring_textures_[slot] = texture{};
			return Error{ErrorCode::UnsupportedFormat,
				"GPU blit requires exact format match between source and ring texture"};
		}

		void *dst_native = detail::backend::get_native_texture(impl_->ring_textures_[slot]);
		if (!dst_native) {
			return Error{ErrorCode::BackendError, "no native handle on ring texture"};
		}

		auto blit_result = detail::backend::blit_textures(
			impl_->native_device_, native_texture, dst_native, width, height);
		if (!blit_result.ok()) return blit_result.error();

		impl_->slot_in_use_[slot] = true;
		detail::write_global_metadata(*impl_->state,
			width, height, ring_actual, semantic_format,
			channel_swizzle::identity);

		uint64_t resource_id = detail::backend::get_shared_resource_id(impl_->ring_textures_[slot]);
		if (resource_id == detail::kInvalidSharedResourceId) {
			impl_->slot_in_use_[slot] = false;
			return Error{ErrorCode::BackendError, "ring texture has no shared resource"};
		}

		uint64_t frame_number = ++impl_->frame_counter_;
		detail::write_slot_metadata(impl_->state->slots[slot],
			frame_number, resource_id, width, height,
			ring_actual, semantic_format,
			channel_swizzle::identity, impl_->ring_textures_[slot].resolved());
		detail::ipc::atomic_store_release_64(&impl_->state->committed_frame, frame_number);
		detail::ipc::atomic_store_release_32(&impl_->state->committed_slot, slot);
		impl_->slot_in_use_[slot] = false;
	}

	return {};
}

} // namespace nozzle
