// Minimal sender example — publishes RGBA textures

#include <nozzle/nozzle.hpp>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <thread>

static void print_usage(const char *prog) {
    std::fprintf(stderr, "Usage: %s [options]\n", prog);
    std::fprintf(stderr, "Options:\n");
    std::fprintf(stderr, "  -n <name>          sender name (default: example_sender)\n");
    std::fprintf(stderr, "  -w <width>         texture width (default: 256)\n");
    std::fprintf(stderr, "  -h <height>        texture height (default: 256)\n");
    std::fprintf(stderr, "  -f <count>         number of frames (default: -1 = infinite)\n");
    std::fprintf(stderr, "  -d <ms>            delay between frames in ms (default: 16)\n");
    std::fprintf(stderr, "  -a <app_name>      application name (default: nozzle_example)\n");
}

int main(int argc, char *argv[]) {
    const char *name = "example_sender";
    const char *app_name = "nozzle_example";
    uint32_t width = 256;
    uint32_t height = 256;
    int frame_count = -1;
    int delay_ms = 16;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "-n") == 0 && i + 1 < argc) {
            name = argv[++i];
        } else if (std::strcmp(argv[i], "-w") == 0 && i + 1 < argc) {
            width = static_cast<uint32_t>(std::atoi(argv[++i]));
        } else if (std::strcmp(argv[i], "-h") == 0 && i + 1 < argc) {
            height = static_cast<uint32_t>(std::atoi(argv[++i]));
        } else if (std::strcmp(argv[i], "-f") == 0 && i + 1 < argc) {
            frame_count = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "-d") == 0 && i + 1 < argc) {
            delay_ms = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "-a") == 0 && i + 1 < argc) {
            app_name = argv[++i];
        } else if (std::strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else {
            std::fprintf(stderr, "Unknown option: %s\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        }
    }

    nozzle::sender_desc desc{};
    desc.name = name;
    desc.application_name = app_name;

    auto sender_result = nozzle::sender::create(desc);
    if (!sender_result.ok()) {
        std::fprintf(stderr, "Failed to create sender: %s\n",
                     sender_result.error().message.c_str());
        return 1;
    }

    auto sender = std::move(sender_result.value());
    std::printf("Sender created: %s (app: %s)\n", sender.info().name.c_str(), app_name);

    nozzle::metadata_list md;
    md.push_back({"example_key", "hello_from_sender"});
    sender.set_metadata(md);

    nozzle::texture_desc tdesc{};
    tdesc.width = width;
    tdesc.height = height;
    tdesc.format = nozzle::texture_format::rgba8_unorm;

    int frame = 0;
    while (frame_count < 0 || frame < frame_count) {
        auto wf_result = sender.acquire_writable_frame(tdesc);
        if (!wf_result.ok()) {
            std::fprintf(stderr, "Frame %d acquire failed: %s\n",
                         frame, wf_result.error().message.c_str());
            continue;
        }

        auto commit_result = sender.commit_frame(wf_result.value());
        if (!commit_result.ok()) {
            std::fprintf(stderr, "Frame %d commit failed: %s\n",
                         frame, commit_result.error().message.c_str());
            continue;
        }

        std::printf("Published frame %d (%ux%u)\n", frame, width, height);
        ++frame;

        if (delay_ms > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
        }
    }

    std::printf("Done. Published %d frames.\n", frame);
    return 0;
}
