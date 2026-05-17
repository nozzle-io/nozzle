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

    // Publish a nozzle::texture as a shared frame.
    // The texture's underlying shared resource (e.g. IOSurface) is published
    // directly — no copy is made. The caller must keep the texture alive until
    // all receivers have finished reading the frame, or until the next publish
    // replaces this slot in the ring buffer.
    Result<void> publish_external_texture(const texture &tex);

    // Publish a native GPU texture as a shared frame.
    // The texture is borrowed only for the duration of the call.
    // The published frame is a snapshot — implementations copy the texture into
    // sender-owned shared storage before returning. After this function returns,
    // the caller may release, reuse, or modify native_texture.
    Result<void> publish_native_texture(void *native_texture, uint32_t width, uint32_t height, texture_format format);
    Result<void> publish_native_texture(void *native_texture, uint32_t width, uint32_t height, texture_format storage_format, texture_format semantic_format);

    Result<writable_frame> acquire_writable_frame(const texture_desc &desc);
    Result<void> commit_frame(writable_frame &frame);

    sender_info info() const;
    native_device_desc native_device() const;

    Result<void> set_metadata(const metadata_list &metadata);

    bool valid() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace nozzle
