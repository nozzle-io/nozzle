#pragma once

#include "d3d11_fanout_protocol.hpp"
#include <nozzle/pixel_access.hpp>
#include <vector>

namespace fanout_test {

template <typename T> struct com_owner {
    T *ptr{nullptr};
    ~com_owner() { if (ptr) ptr->Release(); }
    com_owner() = default;
    com_owner(const com_owner &) = delete;
    com_owner &operator=(const com_owner &) = delete;
};

struct retained_resources {
    std::vector<ID3D11Texture2D *> textures;
    ~retained_resources() { for (auto *texture : textures) texture->Release(); }
    uint64_t identify(ID3D11Texture2D *texture) {
        for (auto *known : textures) {
            if (known == texture) return reinterpret_cast<uint64_t>(texture);
        }
        texture->AddRef();
        textures.push_back(texture);
        return reinterpret_cast<uint64_t>(texture);
    }
};

inline bool write_pattern(nozzle::writable_frame &frame, uint32_t seed) {
    auto *texture = nozzle::d3d11::get_texture(frame.get_texture());
    if (!texture) return false;
    D3D11_TEXTURE2D_DESC desc{};
    texture->GetDesc(&desc);
    const bool bgra = desc.Format == DXGI_FORMAT_B8G8R8A8_UNORM;
    if (desc.Width != width || desc.Height != height ||
        (!bgra && desc.Format != DXGI_FORMAT_R8G8B8A8_UNORM)) return false;
    uint8_t pixels[width * height * 4]{};
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            for (uint32_t c = 0; c < 4; ++c) {
                const auto semantic = bgra && c != 1 && c != 3 ? 2 - c : c;
                pixels[(y * width + x) * 4 + c] = channel(seed, x, y, semantic);
            }
        }
    }
    com_owner<ID3D11Device> device;
    texture->GetDevice(&device.ptr);
    if (!device.ptr) return false;
    com_owner<ID3D11DeviceContext> context;
    device.ptr->GetImmediateContext(&context.ptr);
    if (!context.ptr) return false;
    com_owner<ID3D11Query> query;
    D3D11_QUERY_DESC query_desc{};
    query_desc.Query = D3D11_QUERY_EVENT;
    if (FAILED(device.ptr->CreateQuery(&query_desc, &query.ptr))) return false;
    context.ptr->UpdateSubresource(texture, 0, nullptr, pixels, width * 4, 0);
    context.ptr->End(query.ptr);
    context.ptr->Flush();
    const auto deadline = clock::now() + std::chrono::seconds(2);
    while (clock::now() < deadline) {
        BOOL finished = FALSE;
        const HRESULT hr = context.ptr->GetData(query.ptr, &finished, sizeof(finished), 0);
        if (hr == S_OK && finished) return true;
        if (FAILED(hr)) return false;
        Sleep(1);
    }
    return false;
}

inline bool verify_pattern(const nozzle::frame &frame, uint32_t seed) {
    const auto info = frame.info();
    if (info.width != width || info.height != height) return false;
    auto mapping = nozzle::lock_frame_pixels_mapping_with_origin(frame, nozzle::texture_origin::top_left);
    if (!mapping.ok()) return false;
    const auto &pixels = mapping.value().pixels();
    const bool bgra = pixels.format == nozzle::texture_format::bgra8_unorm;
    if (pixels.width != width || pixels.height != height ||
        (!bgra && pixels.format != nozzle::texture_format::rgba8_unorm)) return false;
    for (uint32_t y = 0; y < height; ++y) {
        const auto *row = static_cast<const uint8_t *>(pixels.data) + y * pixels.row_stride_bytes;
        for (uint32_t x = 0; x < width; ++x) {
            for (uint32_t c = 0; c < 4; ++c) {
                const auto semantic = bgra && c != 1 && c != 3 ? 2 - c : c;
                if (row[x * 4 + c] != channel(seed, x, y, semantic)) return false;
            }
        }
    }
    return mapping.value().unlock_checked().ok();
}

// CLI: helper actor sender|receiver NAME RING DIRECTORY
// DIRECTORY must exist. ready and N.reply are atomic text records (reply::encode).
// N.command: publish SEED FORBIDDEN_RESOURCE | probe | acquire TIMEOUT SEED HOLD
//            verify SEED | release | quit. Commands are numbered from 1.
inline int run_actor(const char *role, const char *name, uint32_t ring,
                     const std::filesystem::path &directory) {
    nozzle::sender sender;
    nozzle::receiver receiver;
    nozzle::frame held;
    retained_resources resources;
    reply ready;
    const bool producer = std::string(role) == "sender";
    if (producer) {
        nozzle::sender_desc desc{};
        desc.name = name;
        desc.ring_buffer_size = ring;
        auto result = nozzle::sender::create(desc);
        ready.code = result.ok() ? 0 : static_cast<int>(result.error().code);
        if (result.ok()) sender = std::move(result.value());
    } else if (std::string(role) == "receiver") {
        nozzle::receiver_desc desc{};
        desc.name = name;
        auto result = nozzle::receiver::create(desc);
        ready.code = result.ok() ? 0 : static_cast<int>(result.error().code);
        if (result.ok()) receiver = std::move(result.value());
    } else return 40;
    if (!publish_file(directory / "ready", ready.encode()) || ready.code != 0) return 41;
    for (uint32_t sequence = 1; ; ++sequence) {
        std::string command;
        const auto deadline = clock::now() + std::chrono::seconds(30);
        while (!read_file(directory / (std::to_string(sequence) + ".command"), command)) {
            if (clock::now() >= deadline) return 42;
            Sleep(2);
        }
        std::istringstream input(command);
        std::string operation;
        input >> operation;
        reply response;
        const auto start = clock::now();
        if (operation == "quit") return 0;
        if (producer && (operation == "publish" || operation == "probe")) {
            uint32_t seed = 0;
            uint64_t forbidden = 0;
            if (operation == "publish" && !(input >> seed >> forbidden)) return 43;
            nozzle::texture_desc desc{};
            desc.width = width;
            desc.height = height;
            desc.format = nozzle::texture_format::rgba8_unorm;
            auto acquired = sender.acquire_writable_frame(desc);
            response.acquire_ms = milliseconds(start);
            response.acquire_code = acquired.ok() ? 0 : static_cast<int>(acquired.error().code);
            response.code = response.acquire_code;
            if (acquired.ok()) {
                auto &frame = acquired.value();
                auto *texture = nozzle::d3d11::get_texture(frame.get_texture());
                if (!texture) return 44;
                // Keep COM identity alive, not a lock, so recreation cannot be
                // mistaken for allocator reuse of an old texture pointer.
                response.resource = resources.identify(texture);
                if (operation == "probe" || response.resource == forbidden) {
                    const auto discarded = sender.discard_frame(frame);
                    if (!discarded.ok()) return 45;
                    if (operation != "probe") response.code = -2;
                } else {
                    response.wrote = true;
                    // Do not discard/reuse a potentially in-flight texture on
                    // query failure. Parent tears down this failed actor.
                    if (!write_pattern(frame, seed)) return 46;
                    const auto commit_start = clock::now();
                    auto committed = sender.commit_frame(frame);
                    response.commit_ms = milliseconds(commit_start);
                    response.commit_code = committed.ok() ? 0 : static_cast<int>(committed.error().code);
                    response.code = response.commit_code;
                    if (frame.valid() && !sender.discard_frame(frame).ok()) return 47;
                }
            }
        } else if (!producer && operation == "acquire") {
            uint32_t timeout = 0, seed = 0, hold = 0;
            if (!(input >> timeout >> seed >> hold) || held.valid()) return 48;
            nozzle::acquire_desc desc{};
            desc.timeout_ms = timeout;
            auto acquired = receiver.acquire_frame(desc);
            response.acquire_ms = milliseconds(start);
            response.code = response.acquire_code = acquired.ok() ? 0 : static_cast<int>(acquired.error().code);
            if (acquired.ok()) {
                response.frame = acquired.value().info().frame_index;
                response.pixels_ok = verify_pattern(acquired.value(), seed);
                if (hold) held = std::move(acquired.value());
            }
        } else if (!producer && operation == "verify") {
            uint32_t seed = 0;
            if (!(input >> seed) || !held.valid()) return 49;
            response.code = 0;
            response.frame = held.info().frame_index;
            response.pixels_ok = verify_pattern(held, seed);
        } else if (!producer && operation == "release") {
            held.release();
            response.code = 0;
        } else return 50;
        response.total_ms = milliseconds(start);
        if (!publish_file(directory / (std::to_string(sequence) + ".reply"), response.encode())) return 51;
    }
}

} // namespace fanout_test
