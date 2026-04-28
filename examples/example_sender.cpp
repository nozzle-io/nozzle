// Minimal sender example — publishes a 256x256 RGBA texture

#include <bbb/nozzle/nozzle.hpp>

#include <cstdio>
#include <cstdlib>

int main() {
    bbb::nozzle::sender_desc desc{};
    desc.name = "example_sender";
    desc.application_name = "nozzle_example";

    auto sender_result = bbb::nozzle::sender::create(desc);
    if (!sender_result.ok()) {
        std::fprintf(stderr, "Failed to create sender: %s\n",
                     sender_result.error().message.c_str());
        return 1;
    }

    auto sender = std::move(sender_result.value());
    std::printf("Sender created: %s\n", sender.info().name.c_str());

    bbb::nozzle::metadata_list md;
    md.push_back({"example_key", "hello_from_sender"});
    sender.set_metadata(md);

    bbb::nozzle::texture_desc tdesc{};
    tdesc.width = 256;
    tdesc.height = 256;
    tdesc.format = bbb::nozzle::texture_format::rgba8_unorm;

    for (int i = 0; i < 100; ++i) {
        auto wf_result = sender.acquire_writable_frame(tdesc);
        if (!wf_result.ok()) {
            std::fprintf(stderr, "Frame %d acquire failed: %s\n",
                         i, wf_result.error().message.c_str());
            continue;
        }

        auto commit_result = sender.commit_frame(wf_result.value());
        if (!commit_result.ok()) {
            std::fprintf(stderr, "Frame %d commit failed: %s\n",
                         i, commit_result.error().message.c_str());
            continue;
        }

        std::printf("Published frame %d\n", i);
    }

    std::printf("Done.\n");
    return 0;
}
