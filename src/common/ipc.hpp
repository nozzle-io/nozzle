// nozzle - ipc.hpp - Platform abstraction for IPC primitives

#pragma once

#include <nozzle/result.hpp>

#include <cstddef>
#include <cstdint>

#if defined(_MSC_VER)
#include <atomic>
#endif

namespace nozzle {
namespace detail {
namespace ipc {

struct shm_handle {
#if defined(_WIN32)
    void *handle{nullptr};
#else
    int fd{-1};
#endif
    bool valid() const noexcept;
};

using pid_type = uint64_t;

Result<shm_handle> shm_create(const char *name, std::size_t size);
Result<shm_handle> shm_open_ro(const char *name);
Result<shm_handle> shm_open_rw(const char *name);
Result<void *> shm_map(shm_handle &h, std::size_t size, bool read_only);
void shm_unmap(void *ptr, std::size_t size) noexcept;
void shm_close(shm_handle &h) noexcept;
void shm_unlink(const char *name) noexcept;
Result<std::size_t> shm_get_size(const shm_handle &h);

struct dir_lock {
#if defined(_WIN32)
    void *handle{nullptr};
#else
    int fd{-1};
    bool locked{false};
#endif
    bool valid() const noexcept;
};

Result<dir_lock> dir_lock_acquire(const char *name, bool exclusive);
void dir_lock_release(dir_lock &lock) noexcept;

pid_type current_pid() noexcept;
bool is_pid_alive(pid_type pid) noexcept;

void random_bytes(void *buf, std::size_t len) noexcept;

uint64_t monotonic_ns() noexcept;

#if defined(_MSC_VER)

inline uint32_t atomic_load_relaxed(const uint32_t *ptr) noexcept {
    return *reinterpret_cast<const volatile uint32_t *>(ptr);
}

inline uint64_t atomic_load_relaxed(const uint64_t *ptr) noexcept {
    return *reinterpret_cast<const volatile uint64_t *>(ptr);
}

inline void atomic_store_relaxed(uint32_t *ptr, uint32_t val) noexcept {
    *reinterpret_cast<volatile uint32_t *>(ptr) = val;
}

inline void atomic_store_relaxed(uint64_t *ptr, uint64_t val) noexcept {
    *reinterpret_cast<volatile uint64_t *>(ptr) = val;
}

inline void atomic_store_release_32(uint32_t *ptr, uint32_t val) noexcept {
    *reinterpret_cast<volatile uint32_t *>(ptr) = val;
    std::atomic_thread_fence(std::memory_order_release);
}

inline void atomic_store_release_64(uint64_t *ptr, uint64_t val) noexcept {
    *reinterpret_cast<volatile uint64_t *>(ptr) = val;
    std::atomic_thread_fence(std::memory_order_release);
}

inline void atomic_fence_acquire() noexcept {
    std::atomic_thread_fence(std::memory_order_acquire);
}

inline void atomic_fence_release() noexcept {
    std::atomic_thread_fence(std::memory_order_release);
}

#else

inline uint32_t atomic_load_relaxed(const uint32_t *ptr) noexcept {
    return __atomic_load_n(ptr, __ATOMIC_RELAXED);
}

inline uint64_t atomic_load_relaxed(const uint64_t *ptr) noexcept {
    return __atomic_load_n(ptr, __ATOMIC_RELAXED);
}

inline void atomic_store_relaxed(uint32_t *ptr, uint32_t val) noexcept {
    __atomic_store_n(ptr, val, __ATOMIC_RELAXED);
}

inline void atomic_store_relaxed(uint64_t *ptr, uint64_t val) noexcept {
    __atomic_store_n(ptr, val, __ATOMIC_RELAXED);
}

inline void atomic_store_release_32(uint32_t *ptr, uint32_t val) noexcept {
    __atomic_store_n(ptr, val, __ATOMIC_RELEASE);
}

inline void atomic_store_release_64(uint64_t *ptr, uint64_t val) noexcept {
    __atomic_store_n(ptr, val, __ATOMIC_RELEASE);
}

inline void atomic_fence_acquire() noexcept {
    __atomic_thread_fence(__ATOMIC_ACQUIRE);
}

inline void atomic_fence_release() noexcept {
    __atomic_thread_fence(__ATOMIC_RELEASE);
}

#endif

} // namespace ipc
} // namespace detail
} // namespace nozzle
