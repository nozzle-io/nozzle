#pragma once

#include <bbb/nozzle/result.hpp>
#include <bbb/nozzle/types.hpp>

#include <memory>

namespace bbb::nozzle {

class device;

namespace detail {
    device make_device_from_backend(void *);
}

class device {
public:
    static Result<device> default_device();

    device();
    ~device();

    device(const device &) = delete;
    device &operator=(const device &) = delete;
    device(device &&) noexcept;
    device &operator=(device &&) noexcept;

    bool supports_format(texture_format format, texture_usage usage) const;
    bool supports_native_format(uint32_t native_format, texture_usage usage) const;

    bool valid() const;

private:
    friend device detail::make_device_from_backend(void *);
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace bbb::nozzle
