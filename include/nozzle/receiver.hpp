#pragma once

#include <nozzle/types.hpp>
#include <nozzle/frame.hpp>
#include <nozzle/device.hpp>

#include <memory>

namespace nozzle {

class receiver {
public:
    static Result<receiver> create(const receiver_desc &desc);

    ~receiver();

    receiver();
    receiver(const receiver &) = delete;
    receiver &operator=(const receiver &) = delete;
    receiver(receiver &&) noexcept;
    receiver &operator=(receiver &&) noexcept;

    // Frame acquisition
    Result<frame> acquire_frame();
    Result<frame> acquire_frame(const acquire_desc &desc);

    // Connected sender info
    connected_sender_info connected_info() const;
    metadata_list sender_metadata() const;
    bool is_connected() const;

    bool valid() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace nozzle
