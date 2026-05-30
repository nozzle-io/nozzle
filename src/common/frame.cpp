// nozzle - frame.cpp - Frame and writable_frame implementation

#include <nozzle/frame.hpp>
#include <nozzle/result.hpp>

#include "frame_helpers.hpp"
#include "backends/backend_dispatch.hpp"

#include <new>
#include <utility>

namespace nozzle {

frame::frame() = default;
frame::~frame() {
    release();
}
frame::frame(frame &&) noexcept = default;
frame &frame::operator=(frame &&other) noexcept {
    if (this != &other) {
        release();
        impl_ = std::move(other.impl_);
    }
    return *this;
}

frame_info frame::info() const {
    if (!impl_) {
        return {};
    }
    return impl_->info_;
}

const texture &frame::get_texture() const {
    static const texture empty{};
    if (!impl_) {
        return empty;
    }
    return impl_->tex_;
}

bool frame::valid() const {
    return impl_ && impl_->valid_;
}

Result<texture> frame::clone_to_owned_texture(device &) const {
    return Error{ErrorCode::ResourceCreationFailed, "clone_to_owned_texture: not yet implemented"};
}

void frame::release() {
    if (impl_) {
        if (impl_->valid_) {
            void *native_texture = detail::backend::get_native_texture(impl_->tex_);
            if (native_texture) {
                detail::backend::release_texture_sync(native_texture, impl_->slot_index_);
            }
        }
        impl_->valid_ = false;
        impl_->tex_ = texture{};
    }
}

Result<void> frame::copy_to_native_texture(void *native_texture, uint32_t width, uint32_t height, texture_format format) const {
    if (!native_texture) {
        return Error{ErrorCode::InvalidArgument, "native_texture is null"};
    }
    if (!impl_ || !impl_->valid_) {
        return Error{ErrorCode::InvalidArgument, "frame is not valid"};
    }

    void *src_native = detail::backend::get_native_texture(impl_->tex_);
    if (!src_native) {
        return Error{ErrorCode::BackendError, "frame texture has no native handle"};
    }

    auto *dev_ptr = detail::backend::get_default_device();
    return detail::backend::blit_textures(dev_ptr, src_native, native_texture, width, height);
}

writable_frame::writable_frame() noexcept = default;
writable_frame::~writable_frame() = default;
writable_frame::writable_frame(writable_frame &&) noexcept = default;
writable_frame &writable_frame::operator=(writable_frame &&) noexcept = default;

texture &writable_frame::get_texture() {
    static texture empty{};
    if (!impl_) {
        return empty;
    }
    return impl_->tex_;
}

const texture_desc &writable_frame::desc() const {
    static const texture_desc empty{};
    if (!impl_) {
        return empty;
    }
    return impl_->desc_;
}

bool writable_frame::valid() const {
    return impl_ && impl_->valid_;
}

namespace detail {

writable_cpu_mapping_state_ref::writable_cpu_mapping_state_ref(writable_cpu_mapping_state *state) noexcept
    : state_{state}
{}

writable_cpu_mapping_state_ref::writable_cpu_mapping_state_ref(const writable_cpu_mapping_state_ref &other) noexcept
    : state_{other.state_}
{
    if (state_) {
        state_->reference_count.fetch_add(1, std::memory_order_relaxed);
    }
}

writable_cpu_mapping_state_ref &writable_cpu_mapping_state_ref::operator=(const writable_cpu_mapping_state_ref &other) noexcept {
    if (this != &other) {
        writable_cpu_mapping_state *next = other.state_;
        if (next) {
            next->reference_count.fetch_add(1, std::memory_order_relaxed);
        }
        reset();
        state_ = next;
    }
    return *this;
}

writable_cpu_mapping_state_ref::writable_cpu_mapping_state_ref(writable_cpu_mapping_state_ref &&other) noexcept
    : state_{other.state_}
{
    other.state_ = nullptr;
}

writable_cpu_mapping_state_ref &writable_cpu_mapping_state_ref::operator=(writable_cpu_mapping_state_ref &&other) noexcept {
    if (this != &other) {
        reset();
        state_ = other.state_;
        other.state_ = nullptr;
    }
    return *this;
}

writable_cpu_mapping_state_ref::~writable_cpu_mapping_state_ref() noexcept {
    reset();
}

writable_cpu_mapping_state *writable_cpu_mapping_state_ref::get() const noexcept {
    return state_;
}

writable_cpu_mapping_state_ref::operator bool() const noexcept {
    return state_ != nullptr;
}

void writable_cpu_mapping_state_ref::reset() noexcept {
    if (!state_) {
        return;
    }
    writable_cpu_mapping_state *state = state_;
    state_ = nullptr;
    if (state->reference_count.fetch_sub(1, std::memory_order_acq_rel) == 1) {
        delete state;
    }
}

namespace {

Result<writable_cpu_mapping_state_ref> make_writable_cpu_mapping_state_ref() {
    auto *state = new (std::nothrow) writable_cpu_mapping_state{};
    if (!state) {
        return Error{ErrorCode::ResourceCreationFailed, "failed to allocate writable CPU mapping state"};
    }
    return writable_cpu_mapping_state_ref{state};
}

void set_writable_cpu_mapping_state(
    writable_cpu_mapping_state_ref &state_ref,
    bool active,
    bool unlock_failed
) {
    auto *state = state_ref.get();
    if (!state) {
        return;
    }
    std::lock_guard<std::mutex> lock(state->mutex);
    state->active = active;
    state->unlock_failed = unlock_failed;
}

} // anonymous namespace

frame make_frame(texture tex, frame_info info) {
    return make_frame(std::move(tex), info, 0);
}

frame make_frame(texture tex, frame_info info, uint32_t slot_index) {
    frame f;
    f.impl_ = std::make_unique<frame::Impl>();
    f.impl_->info_ = info;
    f.impl_->slot_index_ = slot_index;
    f.impl_->valid_ = tex.valid();
    f.impl_->tex_ = std::move(tex);
    return f;
}

Result<writable_frame> make_writable_frame(texture tex, texture_desc desc, uint32_t slot_index) {
    auto state_result = make_writable_cpu_mapping_state_ref();
    if (!state_result.ok()) {
        return state_result.error();
    }

    auto *impl = new (std::nothrow) writable_frame::Impl();
    if (!impl) {
        return Error{ErrorCode::ResourceCreationFailed, "failed to allocate writable frame state"};
    }

    writable_frame f;
    f.impl_.reset(impl);
    f.impl_->desc_ = desc;
    f.impl_->slot_index_ = slot_index;
    f.impl_->valid_ = tex.valid();
    f.impl_->tex_ = std::move(tex);
    f.impl_->cpu_mapping_state_ = std::move(state_result.value());
    return f;
}

uint32_t get_writable_frame_slot(const writable_frame &f) {
    if (!f.impl_) {
        return 0;
    }
    return f.impl_->slot_index_;
}

const void *get_writable_frame_state_token(const writable_frame &f) {
    return f.impl_.get();
}

bool writable_frame_cpu_mapping_active(const writable_frame &f) {
    if (!f.impl_ || !f.impl_->cpu_mapping_state_) {
        return false;
    }
    auto *state = f.impl_->cpu_mapping_state_.get();
    std::lock_guard<std::mutex> lock(state->mutex);
    return state->active;
}

bool writable_frame_cpu_unlock_failed(const writable_frame &f) {
    if (!f.impl_ || !f.impl_->cpu_mapping_state_) {
        return false;
    }
    auto *state = f.impl_->cpu_mapping_state_.get();
    std::lock_guard<std::mutex> lock(state->mutex);
    return state->unlock_failed;
}

void mark_writable_frame_cpu_mapping_active(writable_frame &f) {
    if (!f.impl_ || !f.impl_->cpu_mapping_state_) {
        return;
    }
    auto *state = f.impl_->cpu_mapping_state_.get();
    std::lock_guard<std::mutex> lock(state->mutex);
    state->active = true;
    state->unlock_failed = false;
    state->generation += 1;
}

void mark_writable_frame_cpu_mapping_unlocked(writable_frame &f) {
    if (!f.impl_) {
        return;
    }
    set_writable_cpu_mapping_state(f.impl_->cpu_mapping_state_, false, false);
}

void mark_writable_frame_cpu_unlock_failed(writable_frame &f) {
    if (!f.impl_) {
        return;
    }
    set_writable_cpu_mapping_state(f.impl_->cpu_mapping_state_, false, true);
}

Result<writable_cpu_mapping_state_ref> begin_writable_frame_cpu_mapping(writable_frame &f) {
    if (!f.impl_ || !f.impl_->cpu_mapping_state_) {
        return Error{ErrorCode::InvalidArgument, "writable_frame is not valid"};
    }

    writable_cpu_mapping_state_ref state_ref = f.impl_->cpu_mapping_state_;
    auto *state = state_ref.get();
    std::lock_guard<std::mutex> lock(state->mutex);
    if (state->active) {
        return Error{ErrorCode::InvalidArgument, "active writable pixel mapping already exists for this frame"};
    }
    if (state->unlock_failed) {
        return Error{ErrorCode::BackendError, "writable frame CPU unlock previously failed"};
    }
    state->active = true;
    state->unlock_failed = false;
    state->generation += 1;
    return state_ref;
}

void mark_writable_cpu_mapping_unlocked(writable_cpu_mapping_state_ref &state_ref) {
    set_writable_cpu_mapping_state(state_ref, false, false);
}

void mark_writable_cpu_mapping_unlock_failed(writable_cpu_mapping_state_ref &state_ref) {
    set_writable_cpu_mapping_state(state_ref, false, true);
}

} // namespace detail

} // namespace nozzle
