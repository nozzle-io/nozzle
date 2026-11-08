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

#if defined(NOZZLE_ENABLE_TEST_HOOKS) && NOZZLE_ENABLE_TEST_HOOKS
// Internal, calling-thread-only overrides. Zero native_probe_error delegates
// to the OS; nonzero injects errno/GetLastError at kill/OpenProcess.
struct process_test_hooks {
    void *context{nullptr};
    Result<uint64_t> (*token)(pid_type, void *){nullptr};
    uint64_t (*clock_ns)(void *) noexcept{nullptr};
    uint32_t (*native_probe_error)(pid_type, void *) noexcept{nullptr};
};

class scoped_process_test_hooks {
public:
    explicit scoped_process_test_hooks(process_test_hooks hooks) noexcept;
    ~scoped_process_test_hooks();
    scoped_process_test_hooks(const scoped_process_test_hooks &) = delete;
    scoped_process_test_hooks &operator=(const scoped_process_test_hooks &) = delete;

private:
    process_test_hooks hooks_;
    const process_test_hooks *previous_;
};
#endif

// Existing objects are opened unchanged; created is true only on successful creation.
Result<shm_handle> shm_create(const char *name, std::size_t size, bool *created = nullptr);
// POSIX only: allocate an existing zero-length object without replacing it.
// Caller holds the exclusive directory lock and process mutex through initialization.
Result<void> shm_initialize_empty(shm_handle &h, std::size_t size);
Result<shm_handle> shm_open_ro(const char *name);
Result<shm_handle> shm_open_rw(const char *name);
Result<void *> shm_map(shm_handle &h, std::size_t size, bool read_only);
void shm_unmap(void *ptr, std::size_t size) noexcept;
void shm_close(shm_handle &h) noexcept;
void shm_unlink(const char *name) noexcept;
// POSIX only. Ownership is an effective-UID check, not object authentication.
Result<bool> shm_owned_by_current_user(const shm_handle &handle);
// Caller holds the directory lock and has revalidated the sender identity.
// An absent name returns SenderNotFound; other unlink failures remain errors.
Result<void> shm_unlink_checked(const char *name);
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
// Native start token: SenderClosed means observed absence/exit, BackendError
// means unknown, and InvalidArgument means an invalid PID. Never returns zero.
// Linux comparisons require compatible PID/proc/time namespaces.
Result<uint64_t> process_instance_token(pid_type pid);

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
