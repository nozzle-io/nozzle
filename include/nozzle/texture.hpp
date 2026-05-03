#pragma once

#include <nozzle/types.hpp>

#include <memory>

namespace nozzle {

class texture;

namespace detail {
    texture make_texture_from_backend(void *, void *, uint32_t, uint32_t, uint32_t, uint8_t = 0, const native_format_desc * = nullptr);
    void *get_texture_native(const texture &);
    void *get_surface_native(const texture &);
}

class texture {
public:
    texture();
    ~texture();

    texture(const texture &) = delete;
    texture &operator=(const texture &) = delete;
    texture(texture &&) noexcept;
    texture &operator=(texture &&) noexcept;

    const texture_desc &desc() const;
    const resolved_texture_format &resolved() const;
    texture_layout layout() const;
    bool valid() const;

private:
    friend texture detail::make_texture_from_backend(void *, void *, uint32_t, uint32_t, uint32_t, uint8_t, const native_format_desc *);
    friend void *detail::get_texture_native(const texture &);
    friend void *detail::get_surface_native(const texture &);
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace nozzle
