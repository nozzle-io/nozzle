#pragma once

#include <nozzle/sender.hpp>
#include <nozzle/receiver.hpp>
#include <nozzle/frame.hpp>
#include <nozzle/texture.hpp>
#include <nozzle/device.hpp>
#include <nozzle/pixel_access.hpp>

struct NozzleSender {
    std::unique_ptr<nozzle::sender> obj;
    nozzle::sender_info cached_info{};
};

struct NozzleReceiver {
    std::unique_ptr<nozzle::receiver> obj;
    nozzle::connected_sender_info cached_connected_info{};
#if NOZZLE_ENABLE_TEST_HOOKS
    bool use_cached_connected_info_for_tests{false};
#endif
};

struct NozzleFrame {
    std::unique_ptr<nozzle::frame> obj;
    std::unique_ptr<nozzle::writable_frame> writable;
    bool is_writable{false};
};

struct NozzleTexture {
    std::unique_ptr<nozzle::texture> obj;
};

struct NozzleDevice {
    std::unique_ptr<nozzle::device> obj;
};

struct NozzlePixelMapping {
    std::unique_ptr<nozzle::pixel_mapping> obj;
};
