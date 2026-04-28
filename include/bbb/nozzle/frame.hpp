#pragma once

#include <bbb/nozzle/texture.hpp>
#include <bbb/nozzle/device.hpp>

#include <memory>

namespace bbb::nozzle {

class frame;
class writable_frame;

namespace detail {
frame make_frame(texture, frame_info);
writable_frame make_writable_frame(texture, texture_desc, uint32_t);
uint32_t get_writable_frame_slot(const writable_frame &);
}

class frame {
public:
    frame() = default;
    ~frame();

    // Movable only
    frame(const frame &) = delete;
    frame &operator=(const frame &) = delete;
    frame(frame &&) noexcept;
    frame &operator=(frame &&) noexcept;

    // Access
    frame_info info() const;
    const texture &get_texture() const;
    bool valid() const;

    // Clone to owned texture (GPU copy)
    Result<texture> clone_to_owned_texture(device &dev) const;

    // Explicit release (also happens on destruction)
    void release();

private:
    friend frame detail::make_frame(texture, frame_info);
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Writable frame (for sender-side acquire/commit pattern)
class writable_frame {
public:
    writable_frame() noexcept;
    ~writable_frame();

    writable_frame(const writable_frame &) = delete;
    writable_frame &operator=(const writable_frame &) = delete;
    writable_frame(writable_frame &&) noexcept;
    writable_frame &operator=(writable_frame &&) noexcept;

    texture &get_texture();
    const texture_desc &desc() const;
    bool valid() const;

private:
    friend writable_frame detail::make_writable_frame(texture, texture_desc, uint32_t);
    friend uint32_t detail::get_writable_frame_slot(const writable_frame &);
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace bbb::nozzle
