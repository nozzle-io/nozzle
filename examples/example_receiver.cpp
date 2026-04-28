// Minimal receiver example — connects to "example_sender" and reads frames

#include <bbb/nozzle/nozzle.hpp>

#include <cstdio>
#include <cstdlib>
#include <thread>

int main() {
    bbb::nozzle::receiver_desc desc{};
    desc.name = "example_sender";

    auto recv_result = bbb::nozzle::receiver::create(desc);
    if (!recv_result.ok()) {
        std::fprintf(stderr, "Failed to create receiver: %s\n",
                     recv_result.error().message.c_str());
        return 1;
    }

    auto recv = std::move(recv_result.value());
    std::printf("Receiver connected to: %s\n",
                recv.connected_info().name.c_str());

    auto md = recv.sender_metadata();
    for (const auto &kv : md) {
        std::printf("  metadata: %s = %s\n", kv.key.c_str(), kv.value.c_str());
    }

    bbb::nozzle::acquire_desc adesc{};
    adesc.timeout_ms = 5000;

    for (int i = 0; i < 100; ++i) {
        auto frame_result = recv.acquire_frame(adesc);
        if (!frame_result.ok()) {
            std::fprintf(stderr, "Frame %d acquire failed: %s\n",
                         i, frame_result.error().message.c_str());
            continue;
        }

        auto &f = frame_result.value();
        std::printf("Frame %d: %ux%u index=%llu\n",
                     i, f.info().width, f.info().height,
                     (unsigned long long)f.info().frame_index);
    }

    std::printf("Done.\n");
    return 0;
}
