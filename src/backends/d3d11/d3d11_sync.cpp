// nozzle - d3d11_sync.cpp - D3D11 keyed mutex synchronization

#if NOZZLE_HAS_D3D11

#include "d3d11_helpers.hpp"

#include <d3d11.h>
#include <cstdint>
#include <utility>

namespace nozzle {
namespace d3d11 {

namespace {

IDXGIKeyedMutex *get_keyed_mutex(void *shared_texture) noexcept {
    if (!shared_texture) {
        return nullptr;
    }
    auto *texture = static_cast<ID3D11Texture2D *>(shared_texture);
    IDXGIKeyedMutex *mutex = nullptr;
    HRESULT hr = texture->QueryInterface(
        __uuidof(IDXGIKeyedMutex), reinterpret_cast<void **>(&mutex));
    if (hr != S_OK || !mutex) {
        if (mutex) mutex->Release();
        return nullptr;
    }
    return mutex;
}

HRESULT acquire_mutex(IDXGIKeyedMutex *mutex, uint32_t timeout_ms) noexcept {
    // UINT32_MAX is the Win32 infinite-wait sentinel, not a finite duration.
    const DWORD wait_ms = timeout_ms == UINT32_MAX ? UINT32_MAX - 1 : timeout_ms;
    return mutex->AcquireSync(0, wait_ms);
}

mutex_acquire_result classify_acquire(HRESULT hr) noexcept {
    if (hr == S_OK) return mutex_acquire_result::acquired;
    if (hr == static_cast<HRESULT>(WAIT_TIMEOUT)) return mutex_acquire_result::timeout;
    if (hr == static_cast<HRESULT>(WAIT_ABANDONED)) return mutex_acquire_result::abandoned;
    return mutex_acquire_result::failed;
}

Result<void> checked_acquire_result(HRESULT hr) {
    switch (classify_acquire(hr)) {
    case mutex_acquire_result::acquired:
        return {};
    case mutex_acquire_result::timeout:
        return Error{ErrorCode::Timeout, "timeout acquiring D3D11 keyed mutex"};
    case mutex_acquire_result::abandoned:
        return Error{ErrorCode::BackendError, "D3D11 keyed mutex abandoned; resource is invalid"};
    case mutex_acquire_result::failed:
        return Error{ErrorCode::BackendError, "failed to acquire D3D11 keyed mutex"};
    }
    return Error{ErrorCode::BackendError, "invalid D3D11 mutex acquisition result"};
}

} // anonymous namespace

keyed_mutex_lease::~keyed_mutex_lease() noexcept {
    (void)release_native();
}

keyed_mutex_lease::keyed_mutex_lease(keyed_mutex_lease &&other) noexcept
    : mutex_{std::exchange(other.mutex_, nullptr)} {}

keyed_mutex_lease &keyed_mutex_lease::operator=(keyed_mutex_lease &&other) noexcept {
    if (this != &other) {
        (void)release_native();
        mutex_ = std::exchange(other.mutex_, nullptr);
    }
    return *this;
}

mutex_acquire_result keyed_mutex_lease::acquire(void *texture, uint32_t timeout_ms) noexcept {
    if (mutex_) return mutex_acquire_result::failed;
    auto *mutex = get_keyed_mutex(texture);
    if (!mutex) return mutex_acquire_result::failed;
    const auto result = classify_acquire(acquire_mutex(mutex, timeout_ms));
    if (result == mutex_acquire_result::acquired) mutex_ = mutex;
    else mutex->Release();
    return result;
}

long keyed_mutex_lease::release_native() noexcept {
    auto *mutex = std::exchange(mutex_, nullptr);
    if (!mutex) return S_OK;
    const HRESULT hr = mutex->ReleaseSync(0);
    mutex->Release();
    return hr;
}

Result<void> keyed_mutex_lease::release() noexcept {
    if (release_native() != S_OK) {
        return Error{ErrorCode::BackendError, "failed to release D3D11 keyed mutex"};
    }
    return {};
}

bool keyed_mutex_lease::held() const noexcept {
    return mutex_ != nullptr;
}

bool signal_slot_ready(void *shared_texture, uint32_t slot_index) {
    (void)slot_index;
    keyed_mutex_lease lease;
    if (lease.acquire(shared_texture) != mutex_acquire_result::acquired) return false;
    return lease.release().ok();
}

void signal_slot_done(void *shared_texture, uint32_t slot_index) {
    (void)shared_texture;
    (void)slot_index;
}

Result<void> wait_for_slot_checked(void *shared_texture, uint32_t slot_index, uint32_t timeout_ms) {
    (void)slot_index;
    IDXGIKeyedMutex *mutex = get_keyed_mutex(shared_texture);
    if (!mutex) {
        return Error{ErrorCode::BackendError, "D3D11 texture has no usable keyed mutex"};
    }
    const HRESULT hr = acquire_mutex(mutex, timeout_ms);
    // The receiver retains the texture. Drop only this COM reference, keeping
    // successful mutex ownership until frame release calls release_slot.
    mutex->Release();
    return checked_acquire_result(hr);
}

bool wait_for_slot(void *shared_texture, uint32_t slot_index, uint32_t timeout_ms) {
    return wait_for_slot_checked(shared_texture, slot_index, timeout_ms).ok();
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

#endif
