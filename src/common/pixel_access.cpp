// nozzle - pixel_access.cpp - CPU pixel access for frames

#include <bbb/nozzle/pixel_access.hpp>
#include <bbb/nozzle/result.hpp>

#if NOZZLE_PLATFORM_MACOS
#include <bbb/nozzle/backends/metal.hpp>
#include <IOSurface/IOSurface.h>
#endif

namespace bbb::nozzle {

#if NOZZLE_PLATFORM_MACOS

Result<mapped_pixels> lock_frame_pixels(const frame &frm) {
    if (!frm.valid()) {
        return Error{ErrorCode::InvalidArgument, "frame is not valid"};
    }

    auto &tex = frm.get_texture();
    void *surface_ptr = metal::get_io_surface(tex);
    if (!surface_ptr) {
        return Error{ErrorCode::BackendError, "no IOSurface in frame texture"};
    }

    IOSurfaceRef surface = static_cast<IOSurfaceRef>(surface_ptr);
    IOReturn ret = IOSurfaceLock(surface, kIOSurfaceLockReadOnly, nullptr);
    if (ret != kIOReturnSuccess) {
        return Error{ErrorCode::BackendError, "IOSurfaceLock failed"};
    }

    auto info = frm.info();
    return mapped_pixels{
        IOSurfaceGetBaseAddress(surface),
        static_cast<uint32_t>(IOSurfaceGetBytesPerRow(surface)),
        info.width,
        info.height,
        info.format
    };
}

void unlock_frame_pixels(const frame &frm) {
    if (!frm.valid()) { return; }

    auto &tex = frm.get_texture();
    void *surface_ptr = metal::get_io_surface(tex);
    if (!surface_ptr) { return; }

    IOSurfaceUnlock(static_cast<IOSurfaceRef>(surface_ptr), kIOSurfaceLockReadOnly, nullptr);
}

Result<mapped_pixels> lock_writable_pixels(writable_frame &frm) {
    if (!frm.valid()) {
        return Error{ErrorCode::InvalidArgument, "writable_frame is not valid"};
    }

    auto &tex = frm.get_texture();
    void *surface_ptr = metal::get_io_surface(tex);
    if (!surface_ptr) {
        return Error{ErrorCode::BackendError, "no IOSurface in writable_frame texture"};
    }

    IOSurfaceRef surface = static_cast<IOSurfaceRef>(surface_ptr);
    IOReturn ret = IOSurfaceLock(surface, 0, nullptr);
    if (ret != kIOReturnSuccess) {
        return Error{ErrorCode::BackendError, "IOSurfaceLock failed"};
    }

    auto desc = frm.desc();
    return mapped_pixels{
        IOSurfaceGetBaseAddress(surface),
        static_cast<uint32_t>(IOSurfaceGetBytesPerRow(surface)),
        desc.width,
        desc.height,
        desc.format
    };
}

void unlock_writable_pixels(writable_frame &frm) {
    if (!frm.valid()) { return; }

    auto &tex = frm.get_texture();
    void *surface_ptr = metal::get_io_surface(tex);
    if (!surface_ptr) { return; }

    IOSurfaceUnlock(static_cast<IOSurfaceRef>(surface_ptr), 0, nullptr);
}

#else // Windows stubs

Result<mapped_pixels> lock_frame_pixels(const frame &) {
    return Error{ErrorCode::UnsupportedBackend, "lock_frame_pixels not implemented on this platform"};
}

void unlock_frame_pixels(const frame &) {}

Result<mapped_pixels> lock_writable_pixels(writable_frame &) {
    return Error{ErrorCode::UnsupportedBackend, "lock_writable_pixels not implemented on this platform"};
}

void unlock_writable_pixels(writable_frame &) {}

#endif

} // namespace bbb::nozzle
