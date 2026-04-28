// nozzle - d3d11_sync.cpp - D3D11 keyed mutex synchronization

#if NOZZLE_HAS_D3D11

#include <d3d11.h>

namespace bbb {
namespace nozzle {
namespace d3d11 {

static IDXGIKeyedMutex *get_keyed_mutex(void *shared_texture) {
    if (!shared_texture) {
        return nullptr;
    }
    auto *texture = static_cast<ID3D11Texture2D *>(shared_texture);
    IDXGIKeyedMutex *mutex = nullptr;
    HRESULT hr = texture->QueryInterface(
        __uuidof(IDXGIKeyedMutex), reinterpret_cast<void **>(&mutex));
    if (FAILED(hr)) {
        return nullptr;
    }
    return mutex;
}

void signal_slot_ready(void *shared_texture, uint32_t slot_index) {
    (void)slot_index;
    IDXGIKeyedMutex *mutex = get_keyed_mutex(shared_texture);
    if (!mutex) {
        return;
    }
    mutex->AcquireSync(0, INFINITE);
    mutex->ReleaseSync(1);
    mutex->Release();
}

void signal_slot_done(void *shared_texture, uint32_t slot_index) {
    (void)shared_texture;
    (void)slot_index;
}

bool wait_for_slot(void *shared_texture, uint32_t slot_index, uint32_t timeout_ms) {
    (void)slot_index;
    IDXGIKeyedMutex *mutex = get_keyed_mutex(shared_texture);
    if (!mutex) {
        return false;
    }
    DWORD ms = (timeout_ms == 0) ? INFINITE : static_cast<DWORD>(timeout_ms);
    HRESULT hr = mutex->AcquireSync(1, ms);
    mutex->Release();
    return SUCCEEDED(hr);
}

void release_slot(void *shared_texture, uint32_t slot_index) {
    (void)slot_index;
    IDXGIKeyedMutex *mutex = get_keyed_mutex(shared_texture);
    if (!mutex) {
        return;
    }
    mutex->ReleaseSync(0);
    mutex->Release();
}

} // namespace d3d11
} // namespace nozzle
} // namespace bbb

#endif
