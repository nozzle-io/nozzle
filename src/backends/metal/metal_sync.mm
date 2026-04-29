// nozzle - metal_sync.mm - Cross-process IOSurface synchronization for ring buffer

#import <IOSurface/IOSurface.h>

#include <chrono>
#include <cstdint>
#include <thread>

namespace nozzle::metal {

void metal_signal_slot_ready(IOSurfaceRef surface) {
    @autoreleasepool {
        IOSurfaceLock(surface, 0, nullptr);
    }
}

void metal_signal_slot_done(IOSurfaceRef surface) {
    @autoreleasepool {
        IOSurfaceUnlock(surface, 0, nullptr);
    }
}

bool metal_wait_for_slot(IOSurfaceRef surface, uint32_t timeout_ms) {
    @autoreleasepool {
        using clock = std::chrono::steady_clock;
        const auto deadline = clock::now() + std::chrono::milliseconds(timeout_ms);
        constexpr uint32_t kPollIntervalUs = 500;

        while (true) {
            IOReturn result = IOSurfaceLock(
                surface,
                kIOSurfaceLockReadOnly | kIOSurfaceLockAvoidSync,
                nullptr
            );
            if (result == kIOReturnSuccess) {
                return true;
            }
            if (timeout_ms == 0) {
                return false;
            }
            if (clock::now() >= deadline) {
                return false;
            }
            std::this_thread::sleep_for(std::chrono::microseconds(kPollIntervalUs));
        }
    }
}

void metal_release_slot(IOSurfaceRef surface) {
    @autoreleasepool {
        IOSurfaceUnlock(surface, kIOSurfaceLockReadOnly, nullptr);
    }
}

} // namespace nozzle::metal

extern "C" {

void nozzle_metal_signal_slot_ready(void *surface) {
    nozzle::metal::metal_signal_slot_ready((IOSurfaceRef)surface);
}

void nozzle_metal_signal_slot_done(void *surface) {
    nozzle::metal::metal_signal_slot_done((IOSurfaceRef)surface);
}

bool nozzle_metal_wait_for_slot(void *surface, uint32_t timeout_ms) {
    return nozzle::metal::metal_wait_for_slot((IOSurfaceRef)surface, timeout_ms);
}

void nozzle_metal_release_slot(void *surface) {
    nozzle::metal::metal_release_slot((IOSurfaceRef)surface);
}

} // extern "C"
