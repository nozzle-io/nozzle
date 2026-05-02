// Minimal receiver example — connects to a named sender and reads frames

#include <nozzle/nozzle.hpp>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <csignal>
#include <thread>

static volatile std::sig_atomic_t g_running = 1;

static void signal_handler(int) {
    g_running = 0;
}

static void print_usage(const char *prog) {
    std::fprintf(stderr, "Usage: %s [options]\n", prog);
    std::fprintf(stderr, "Options:\n");
    std::fprintf(stderr, "  -n <name>          sender name to connect to (default: example_sender)\n");
    std::fprintf(stderr, "  -f <count>         number of frames (default: -1 = infinite)\n");
    std::fprintf(stderr, "  -t <ms>            acquire timeout in ms (default: 5000)\n");
    std::fprintf(stderr, "  -a <app_name>      application name (default: nozzle_example)\n");
    std::fprintf(stderr, "  -w                 wait for sender to appear (retry forever)\n");
}

int main(int argc, char *argv[]) {
    const char *name = "example_sender";
    const char *app_name = "nozzle_example";
    int frame_count = -1;
    uint32_t timeout_ms = 5000;
    bool wait_for_sender = false;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "-n") == 0 && i + 1 < argc) {
            name = argv[++i];
        } else if (std::strcmp(argv[i], "-f") == 0 && i + 1 < argc) {
            frame_count = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "-t") == 0 && i + 1 < argc) {
            timeout_ms = static_cast<uint32_t>(std::atoi(argv[++i]));
        } else if (std::strcmp(argv[i], "-a") == 0 && i + 1 < argc) {
            app_name = argv[++i];
        } else if (std::strcmp(argv[i], "-w") == 0) {
            wait_for_sender = true;
        } else if (std::strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else {
            std::fprintf(stderr, "Unknown option: %s\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        }
    }

    std::signal(SIGINT, signal_handler);

    nozzle::receiver_desc desc{};
    desc.name = name;
    desc.application_name = app_name;

    auto recv_result = nozzle::receiver::create(desc);
    if (!recv_result.ok()) {
        if (wait_for_sender) {
            std::printf("Waiting for sender \"%s\"...\n", name);
        } else {
            std::fprintf(stderr, "Failed to create receiver: %s\n",
                         recv_result.error().message.c_str());
            return 1;
        }
    }

    auto recv = std::move(recv_result.value());

    while (!recv.valid() && wait_for_sender && g_running) {
        recv_result = nozzle::receiver::create(desc);
        if (recv_result.ok()) {
            recv = std::move(recv_result.value());
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    if (!recv.valid()) {
        std::fprintf(stderr, "Failed to connect to sender: %s\n", name);
        return 1;
    }

    std::printf("Receiver connected to: %s\n",
                recv.connected_info().name.c_str());

    auto md = recv.sender_metadata();
    for (const auto &kv : md) {
        std::printf("  metadata: %s = %s\n", kv.key.c_str(), kv.value.c_str());
    }

    nozzle::acquire_desc adesc{};
    adesc.timeout_ms = timeout_ms;

    int frame = 0;
    while (g_running && (frame_count < 0 || frame < frame_count)) {
        auto frame_result = recv.acquire_frame(adesc);
        if (!frame_result.ok()) {
            std::fprintf(stderr, "Frame %d acquire failed: %s\n",
                         frame, frame_result.error().message.c_str());
            ++frame;
            continue;
        }

        auto &f = frame_result.value();
        std::printf("Frame %d: %ux%u index=%llu\n",
                     frame, f.info().width, f.info().height,
                     (unsigned long long)f.info().frame_index);
        ++frame;
    }

    std::printf("Done. Received %d frames.\n", frame);
    return 0;
}
