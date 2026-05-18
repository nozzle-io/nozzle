#if NOZZLE_HAS_D3D11

#include <nozzle/nozzle.hpp>
#include <nozzle/backends/d3d11.hpp>
#include "backends/d3d11/d3d11_helpers.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <utility>

namespace {

constexpr uint32_t k_width = 4;
constexpr uint32_t k_height = 4;
constexpr uint32_t k_pixel = 0xFF3366CCu;

bool create_device(D3D_DRIVER_TYPE type, ID3D11Device **device, ID3D11DeviceContext **context) {
    D3D_FEATURE_LEVEL feature_level{};
    HRESULT hr = D3D11CreateDevice(
        nullptr, type, nullptr,
        D3D11_CREATE_DEVICE_BGRA_SUPPORT,
        nullptr, 0, D3D11_SDK_VERSION,
        device, &feature_level, context);
    return SUCCEEDED(hr);
}

ID3D11Texture2D *create_source_texture(ID3D11Device *device) {
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = k_width;
    desc.Height = k_height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;

    ID3D11Texture2D *texture = nullptr;
    HRESULT hr = device->CreateTexture2D(&desc, nullptr, &texture);
    return SUCCEEDED(hr) ? texture : nullptr;
}

void fill_texture(ID3D11DeviceContext *context, ID3D11Texture2D *texture) {
    uint32_t pixels[k_width * k_height]{};
    for (uint32_t i = 0; i < k_width * k_height; ++i) {
        pixels[i] = k_pixel;
    }
    context->UpdateSubresource(texture, 0, nullptr, pixels, k_width * sizeof(uint32_t), 0);
    context->Flush();
}

bool write_ready_file(const char *path, uint64_t handle_value) {
    std::FILE *file = std::fopen(path, "w");
    if (!file) {
        return false;
    }
    std::fprintf(file, "%llu\n", static_cast<unsigned long long>(handle_value));
    std::fclose(file);
    return true;
}

int run_raw_sender(const char *ready_file) {
    ID3D11Device *device = nullptr;
    ID3D11DeviceContext *context = nullptr;
    if (!create_device(D3D_DRIVER_TYPE_WARP, &device, &context)) {
        return 10;
    }

    auto texture_result = nozzle::d3d11::create_shared_texture(
        device, k_width, k_height, static_cast<uint32_t>(nozzle::texture_format::bgra8_unorm));
    if (!texture_result.ok()) {
        context->Release();
        device->Release();
        return 11;
    }
    nozzle::texture shared_texture = std::move(texture_result.value());
    ID3D11Texture2D *texture = nozzle::d3d11::get_texture(shared_texture);
    if (!texture) {
        context->Release();
        device->Release();
        return 12;
    }

    IDXGIKeyedMutex *mutex = nullptr;
    HRESULT hr = texture->QueryInterface(__uuidof(IDXGIKeyedMutex), reinterpret_cast<void **>(&mutex));
    if (FAILED(hr) || !mutex) {
        context->Release();
        device->Release();
        return 13;
    }
    hr = mutex->AcquireSync(0, 1000);
    if (hr != S_OK) {
        mutex->Release();
        context->Release();
        device->Release();
        return 14;
    }
    fill_texture(context, texture);
    hr = mutex->ReleaseSync(1);
    mutex->Release();
    if (hr != S_OK) {
        context->Release();
        device->Release();
        return 15;
    }

    HANDLE handle = nozzle::d3d11::get_shared_handle(shared_texture);
    if (!handle || !write_ready_file(ready_file, reinterpret_cast<uint64_t>(handle))) {
        context->Release();
        device->Release();
        return 16;
    }

    Sleep(30000);
    context->Release();
    device->Release();
    return 0;
}

int run_public_sender(const char *sender_name, const char *ready_file) {
    nozzle::sender_desc sender_desc{};
    sender_desc.name = sender_name;
    sender_desc.application_name = "d3d11_cross_process_sender";
    sender_desc.ring_buffer_size = 2;

    auto sender_result = nozzle::sender::create(sender_desc);
    if (!sender_result.ok()) {
        return 20;
    }
    auto sender = std::move(sender_result.value());
    auto native = sender.native_device();
    auto *device = static_cast<ID3D11Device *>(native.device);
    if (!device) {
        return 21;
    }
    ID3D11DeviceContext *context = nullptr;
    device->GetImmediateContext(&context);
    if (!context) {
        return 22;
    }

    ID3D11Texture2D *source = create_source_texture(device);
    if (!source) {
        context->Release();
        return 23;
    }
    fill_texture(context, source);

    auto publish_result = sender.publish_native_texture(
        source, k_width, k_height, nozzle::texture_format::bgra8_unorm);
    source->Release();
    context->Release();
    if (!publish_result.ok()) {
        return 24;
    }

    if (!write_ready_file(ready_file, 0)) {
        return 25;
    }

    Sleep(30000);
    return 0;
}

} // namespace

int main(int argc, char **argv) {
    if (argc == 3 && std::strcmp(argv[1], "raw") == 0) {
        return run_raw_sender(argv[2]);
    }
    if (argc == 4 && std::strcmp(argv[1], "public") == 0) {
        return run_public_sender(argv[2], argv[3]);
    }
    return 2;
}

#else
int main() { return 0; }
#endif
