#pragma once

#include <nozzle/types.hpp>
#include <nozzle/texture.hpp>
#include <nozzle/frame.hpp>

#include <memory>

namespace nozzle {

class sender {
public:
    static Result<sender> create(const sender_desc &desc);

    ~sender();

    sender();
    sender(const sender &) = delete;
    sender &operator=(const sender &) = delete;
    sender(sender &&) noexcept;
    sender &operator=(sender &&) noexcept;

    Result<void> publish_external_texture(const texture &tex);

    Result<void> publish_native_texture(void *native_texture, uint32_t width, uint32_t height, texture_format format);

    Result<writable_frame> acquire_writable_frame(const texture_desc &desc);
    Result<void> commit_frame(writable_frame &frame);

    sender_info info() const;
    Result<void> set_metadata(const metadata_list &metadata);

    bool valid() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace nozzle
