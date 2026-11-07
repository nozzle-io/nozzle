#pragma once

#include <nozzle/types.hpp>
#include <nozzle/frame.hpp>
#include <nozzle/device.hpp>

#include <memory>

namespace nozzle {

struct receiver_connection_snapshot {
    connected_sender_info info{};
    bool connected{false};
};

class receiver {
public:
    static Result<receiver> create(const receiver_desc &desc);

    ~receiver();

    receiver();
    receiver(const receiver &) = delete;
    receiver &operator=(const receiver &) = delete;
    receiver(receiver &&) noexcept;
    receiver &operator=(receiver &&) noexcept;

    // Observed sender destruction returns SenderClosed and disconnects this handle.
    // The next acquisition looks up the original name again (SenderNotFound if absent).
    // Already returned frames retain their ownership; destruction does not revoke them.
    Result<frame> acquire_frame();
    Result<frame> acquire_frame(const acquire_desc &desc);

    // Connected sender info
    connected_sender_info connected_info() const;
    // Coherent last-observed state and identity; does not probe current liveness.
    receiver_connection_snapshot connection_snapshot() const;
    metadata_list sender_metadata() const;
    // Last observed connection state; acquisition performs liveness checks.
    bool is_connected() const;

    bool valid() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace nozzle
