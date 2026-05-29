#pragma once

#include <nozzle/texture.hpp>
#include <nozzle/device.hpp>

#include <memory>

namespace nozzle {

class frame;
class writable_frame;

namespace detail {
frame make_frame(texture, frame_info);
frame make_frame(texture, frame_info, uint32_t);
writable_frame make_writable_frame(texture, texture_desc, uint32_t);
uint32_t get_writable_frame_slot(const writable_frame &);
const void *get_writable_frame_state_token(const writable_frame &);
bool writable_frame_cpu_mapping_active(const writable_frame &);
bool writable_frame_cpu_unlock_failed(const writable_frame &);
void mark_writable_frame_cpu_mapping_active(writable_frame &);
void mark_writable_frame_cpu_mapping_unlocked(writable_frame &);
void mark_writable_frame_cpu_unlock_failed(writable_frame &);
}

class frame {
public:
    frame();
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

    Result<texture> clone_to_owned_texture(device &dev) const;

    Result<void> copy_to_native_texture(void *native_texture, uint32_t width, uint32_t height, texture_format format) const;

    void release();

private:
    friend frame detail::make_frame(texture, frame_info);
    friend frame detail::make_frame(texture, frame_info, uint32_t);
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
    friend const void *detail::get_writable_frame_state_token(const writable_frame &);
    friend bool detail::writable_frame_cpu_mapping_active(const writable_frame &);
    friend bool detail::writable_frame_cpu_unlock_failed(const writable_frame &);
    friend void detail::mark_writable_frame_cpu_mapping_active(writable_frame &);
    friend void detail::mark_writable_frame_cpu_mapping_unlocked(writable_frame &);
    friend void detail::mark_writable_frame_cpu_unlock_failed(writable_frame &);
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace nozzle
