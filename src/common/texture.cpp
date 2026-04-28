// nozzle - texture.cpp - Texture abstraction and format handling

#include <bbb/nozzle/texture.hpp>

#if NOZZLE_HAS_METAL
namespace bbb::nozzle::metal {
    void release_mtl_texture_resources(void *texture, void *surface);
} // namespace bbb::nozzle::metal
#elif NOZZLE_HAS_D3D11
namespace bbb::nozzle::d3d11 {
    void release_d3d11_texture_resources(void *texture, void *shared_handle);
} // namespace bbb::nozzle::d3d11
#endif

namespace bbb::nozzle {

struct texture::Impl {
    texture_desc desc{};
    texture_layout layout_{};
#if NOZZLE_HAS_METAL
    void *native_texture{nullptr}; // id<MTLTexture>
    void *native_surface{nullptr}; // IOSurfaceRef
#elif NOZZLE_HAS_D3D11
    void *native_texture{nullptr}; // ID3D11Texture2D*
    void *native_shared_handle{nullptr}; // HANDLE
#endif
    bool valid{false};

    ~Impl() {
#if NOZZLE_HAS_METAL
        if (native_texture || native_surface) {
            metal::release_mtl_texture_resources(native_texture, native_surface);
            native_texture = nullptr;
            native_surface = nullptr;
        }
#elif NOZZLE_HAS_D3D11
        if (native_texture || native_shared_handle) {
            d3d11::release_d3d11_texture_resources(native_texture, native_shared_handle);
            native_texture = nullptr;
            native_shared_handle = nullptr;
        }
#endif
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
    uint32_t pixel_format
) {
    texture t;
    t.impl_ = std::make_unique<texture::Impl>();
#if NOZZLE_HAS_METAL
    t.impl_->native_texture = backend_texture;
    t.impl_->native_surface = backend_surface;
    t.impl_->desc.width = width;
    t.impl_->desc.height = height;
    t.impl_->desc.format = static_cast<texture_format>(pixel_format);
    t.impl_->valid = (backend_texture != nullptr);
#elif NOZZLE_HAS_D3D11
    t.impl_->native_texture = backend_texture;
    t.impl_->native_shared_handle = backend_surface;
    t.impl_->desc.width = width;
    t.impl_->desc.height = height;
    t.impl_->desc.format = static_cast<texture_format>(pixel_format);
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
#if NOZZLE_HAS_METAL
    return t.impl_->native_texture;
#elif NOZZLE_HAS_D3D11
    return t.impl_->native_texture;
#else
    return nullptr;
#endif
}

void *get_surface_native(const texture &t) {
    if (!t.valid()) {
        return nullptr;
    }
#if NOZZLE_HAS_METAL
    return t.impl_->native_surface;
#elif NOZZLE_HAS_D3D11
    return t.impl_->native_shared_handle;
#else
    return nullptr;
#endif
}

} // namespace detail

} // namespace bbb::nozzle
