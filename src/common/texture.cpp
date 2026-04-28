// nozzle - texture.cpp - Texture abstraction and format handling

#include <bbb/nozzle/texture.hpp>

#if NOZZLE_HAS_METAL
namespace bbb::nozzle::metal {
    void release_mtl_texture_resources(void *texture, void *surface);
} // namespace bbb::nozzle::metal
#endif

namespace bbb::nozzle {

struct texture::Impl {
    TextureDesc desc{};
    TextureLayout layout_{};
#if NOZZLE_HAS_METAL
    void *native_texture{nullptr}; // id<MTLTexture>
    void *native_surface{nullptr}; // IOSurfaceRef
#endif
    bool valid{false};

    ~Impl() {
#if NOZZLE_HAS_METAL
        if (native_texture || native_surface) {
            metal::release_mtl_texture_resources(native_texture, native_surface);
            native_texture = nullptr;
            native_surface = nullptr;
        }
#endif
    }
};

texture::texture() {}
texture::~texture() = default;
texture::texture(texture &&) noexcept = default;
texture &texture::operator=(texture &&) noexcept = default;

const TextureDesc &texture::desc() const {
    static const TextureDesc empty{};
    if (!impl_) {
        return empty;
    }
    return impl_->desc;
}

TextureLayout texture::layout() const {
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
    uint32_t pixel_format
) {
    texture t;
    t.impl_ = std::make_unique<texture::Impl>();
#if NOZZLE_HAS_METAL
    t.impl_->native_texture = backend_texture;
    t.impl_->native_surface = backend_surface;
    t.impl_->desc.width = width;
    t.impl_->desc.height = height;
    t.impl_->desc.format = static_cast<TextureFormat>(pixel_format);
    t.impl_->valid = (backend_texture != nullptr);
#else
    (void)backend_texture;
    (void)backend_surface;
    (void)width;
    (void)height;
    (void)pixel_format;
#endif
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

} // namespace bbb::nozzle
