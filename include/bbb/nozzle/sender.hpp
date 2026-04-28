#pragma once

#include <bbb/nozzle/types.hpp>
#include <bbb/nozzle/texture.hpp>
#include <bbb/nozzle/frame.hpp>

#include <memory>

namespace bbb::nozzle {

class sender {
public:
    static Result<sender> create(const sender_desc &desc);

    ~sender();

    sender() = default;
    sender(const sender &) = delete;
    sender &operator=(const sender &) = delete;
    sender(sender &&) noexcept;
    sender &operator=(sender &&) noexcept;

    // Publish user-owned texture
    Result<void> publish_external_texture(const texture &tex);

    // Acquire/commit pattern for nozzle-owned textures
    Result<writable_frame> acquire_writable_frame(const texture_desc &desc);
    Result<void> commit_frame(writable_frame &frame);

    // Info
    sender_info info() const;
    Result<void> set_metadata(const metadata_list &metadata);

    bool valid() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace bbb::nozzle
