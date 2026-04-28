// nozzle - device.cpp - GPU device abstraction and management

#include <bbb/nozzle/device.hpp>
#include <bbb/nozzle/result.hpp>

#if NOZZLE_HAS_METAL
namespace bbb::nozzle::metal {
    void *get_default_mtl_device();
    bool metal_supports_format(void *device, uint32_t format, uint32_t usage);
    void release_mtl_device(void *device);
} // namespace bbb::nozzle::metal
#endif

namespace bbb::nozzle {

struct device::Impl {
#if NOZZLE_HAS_METAL
    void *mtl_device{nullptr}; // id<MTLDevice>, ownership managed by metal backend
#endif
    bool valid{false};

    ~Impl() {
#if NOZZLE_HAS_METAL
        if (mtl_device) {
            metal::release_mtl_device(mtl_device);
            mtl_device = nullptr;
        }
#endif
    }
};

device::device() {}
device::~device() = default;
device::device(device &&) noexcept = default;
device &device::operator=(device &&) noexcept = default;

Result<device> device::default_device() {
#if NOZZLE_HAS_METAL
    device d;
    d.impl_ = std::make_unique<Impl>();
    d.impl_->mtl_device = metal::get_default_mtl_device();
    if (!d.impl_->mtl_device) {
        return Error{ErrorCode::BackendError, "Failed to get default Metal device"};
    }
    d.impl_->valid = true;
    return d;
#else
    return Error{ErrorCode::UnsupportedBackend, "No backend available on this platform"};
#endif
}

bool device::supports_format(TextureFormat format, TextureUsage usage) const {
    if (!impl_ || !impl_->valid) {
        return false;
    }
#if NOZZLE_HAS_METAL
    return metal::metal_supports_format(
        impl_->mtl_device,
        static_cast<uint32_t>(format),
        static_cast<uint32_t>(usage)
    );
#else
    (void)format;
    (void)usage;
    return false;
#endif
}

bool device::supports_native_format(uint32_t native_format, TextureUsage usage) const {
    (void)native_format;
    (void)usage;
    return false; // stub — will be implemented per-backend
}

bool device::valid() const {
    return impl_ && impl_->valid;
}

// Internal factory for backend code to create a device from a native pointer.
// The backend is responsible for the correct __bridge_retained/__bridge_transfer
// semantics around this call.
namespace detail {

device make_device_from_backend(void *backend_ptr) {
    device d;
    d.impl_ = std::make_unique<device::Impl>();
#if NOZZLE_HAS_METAL
    d.impl_->mtl_device = backend_ptr;
    d.impl_->valid = (backend_ptr != nullptr);
#else
    (void)backend_ptr;
#endif
    return d;
}

} // namespace detail

} // namespace bbb::nozzle
