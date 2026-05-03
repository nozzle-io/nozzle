// nozzle - texture.cpp - Texture abstraction and format handling

#include <nozzle/texture.hpp>
#include <nozzle/format_resolve.hpp>

#include "backends/backend_dispatch.hpp"

namespace nozzle {

struct texture::Impl {
	texture_desc desc{};
	texture_layout layout_{};
	resolved_texture_format resolved_{};
	void *native_texture{nullptr};
	void *native_surface{nullptr};
	bool valid{false};

	~Impl() {
		if (native_texture || native_surface) {
			detail::backend::release_texture_resources(native_texture, native_surface);
			native_texture = nullptr;
			native_surface = nullptr;
		}
	}
};

texture::texture() {}
texture::~texture() = default;
texture::texture(texture &&) noexcept = default;
texture &texture::operator=(texture &&) noexcept = default;

const texture_desc &texture::desc() const {
    static const texture_desc empty{};
    if (!impl_) {
        return empty;
    }
    return impl_->desc;
}

const resolved_texture_format &texture::resolved() const {
    static const resolved_texture_format empty{};
    if (!impl_) {
        return empty;
    }
    return impl_->resolved_;
}

texture_layout texture::layout() const {
    if (!impl_) {
        return {};
    }
    return impl_->layout_;
}

bool texture::valid() const {
    return impl_ && impl_->valid;
}

namespace detail {

texture make_texture_from_backend(
    void *backend_texture,
    void *backend_surface,
    uint32_t width,
    uint32_t height,
    uint32_t pixel_format,
    uint8_t channel_swizzle_val,
    const native_format_desc *native_desc
) {
    texture t;
    t.impl_ = std::make_unique<texture::Impl>();
    t.impl_->native_texture = backend_texture;
    t.impl_->native_surface = backend_surface;
    t.impl_->desc.width = width;
    t.impl_->desc.height = height;
    auto storage_fmt = static_cast<texture_format>(pixel_format);
    t.impl_->desc.format = storage_fmt;
    t.impl_->resolved_.storage_format = storage_fmt;
    t.impl_->resolved_.cpu_layout = resolve_cpu_layout(storage_fmt);
    t.impl_->resolved_.sampling = resolve_sampling(storage_fmt);
    if (native_desc) {
        t.impl_->resolved_.native = *native_desc;
        t.impl_->resolved_.source = format_source::native_observed;
    } else {
        t.impl_->resolved_.native.value = pixel_format;
        t.impl_->resolved_.source = format_source::requested;
    }
    t.impl_->desc.swizzle = static_cast<channel_swizzle>(channel_swizzle_val);
    t.impl_->resolved_.swizzle = static_cast<channel_swizzle>(channel_swizzle_val);
    t.impl_->valid = (backend_texture != nullptr);
    return t;
}

void *get_texture_native(const texture &t) {
    if (!t.valid()) {
        return nullptr;
    }
    return t.impl_->native_texture;
}

void *get_surface_native(const texture &t) {
    if (!t.valid()) {
        return nullptr;
    }
    return t.impl_->native_surface;
}

} // namespace detail

} // namespace nozzle
