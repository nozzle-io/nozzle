#pragma once

#include <nozzle/types.hpp>
#include <nozzle/texture.hpp>
#include <nozzle/frame.hpp>

#include <memory>

namespace nozzle {

namespace dma_buf {
struct publish_desc;
}

namespace metal {
struct direct_publish_desc;
}

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
    Result<void> publish_dmabuf_texture(const dma_buf::publish_desc &desc);

    // Publish an IOSurface-backed Metal texture directly.
    //
    // This is live-reference sharing, not a snapshot. The receiver observes the
    // IOSurface contents at read/sample time. The sender retains the
    // IOSurface-backed texture/surface for the published slot until that slot is
    // replaced or the sender is destroyed, but this does not freeze contents.
    //
    // The caller must ensure GPU writes to the texture are complete before
    // relying on receiver-visible contents. No implicit Metal fence,
    // MTLSharedEvent, command-buffer wait, or IOSurfaceLock synchronization is
    // provided. CVPixelBuffer/camera-pool users must retain/lock/protect backing
    // buffers if content stability matters. Non-IOSurface textures are rejected;
    // this API never silently falls back to snapshot blit.
    Result<void> publish_metal_texture_direct(const metal::direct_publish_desc &desc);

    Result<writable_frame> acquire_writable_frame(const texture_desc &desc);
    Result<void> commit_frame(writable_frame &frame);
    Result<void> discard_frame(writable_frame &frame);

    sender_info info() const;
    native_device_desc native_device() const;

    Result<void> set_metadata(const metadata_list &metadata);

    bool valid() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace nozzle
