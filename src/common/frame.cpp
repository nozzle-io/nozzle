// nozzle - frame.cpp - Frame and writable_frame implementation

#include <bbb/nozzle/frame.hpp>
#include <bbb/nozzle/result.hpp>

#include "frame_helpers.hpp"

namespace bbb::nozzle {

frame::~frame() = default;
frame::frame(frame &&) noexcept = default;
frame &frame::operator=(frame &&) noexcept = default;

FrameInfo frame::info() const {
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
        impl_->valid_ = false;
        impl_->tex_ = texture{};
    }
}

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

const TextureDesc &writable_frame::desc() const {
    static const TextureDesc empty{};
    if (!impl_) {
        return empty;
    }
    return impl_->desc_;
}

bool writable_frame::valid() const {
    return impl_ && impl_->valid_;
}

namespace detail {

frame make_frame(texture tex, FrameInfo info) {
    frame f;
    f.impl_ = std::make_unique<frame::Impl>();
    f.impl_->tex_ = std::move(tex);
    f.impl_->info_ = info;
    f.impl_->valid_ = tex.valid();
    return f;
}

writable_frame make_writable_frame(texture tex, TextureDesc desc, uint32_t slot_index) {
    writable_frame f;
    f.impl_ = std::make_unique<writable_frame::Impl>();
    f.impl_->tex_ = std::move(tex);
    f.impl_->desc_ = desc;
    f.impl_->slot_index_ = slot_index;
    f.impl_->valid_ = tex.valid();
    return f;
}

uint32_t get_writable_frame_slot(const writable_frame &f) {
    if (!f.impl_) {
        return 0;
    }
    return f.impl_->slot_index_;
}

} // namespace detail

} // namespace bbb::nozzle
