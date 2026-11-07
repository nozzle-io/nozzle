// nozzle - ipc.cpp - Platform implementations for IPC primitives

#include "ipc.hpp"

#include <cerrno>
#include <cstdio>
#include <cstring>

#if defined(_WIN32)

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#include <new>

#pragma comment(lib, "bcrypt.lib")

#else

#include <fcntl.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#if NOZZLE_PLATFORM_LINUX
#include <sys/random.h>
#endif

#endif

namespace nozzle {
namespace detail {
namespace ipc {

#if defined(_WIN32)

static const char *win_shm_name(const char *name) {
    if (name[0] == '/') {
        return name + 1;
    }
    return name;
}

namespace {

struct mapping_pin {
    mapping_pin *next{nullptr};
    HANDLE handle{nullptr};
    char *name{nullptr};

    ~mapping_pin() {
        if (handle) {
            CloseHandle(handle);
        }
        delete[] name;
    }
};

struct mapping_registry {
    SRWLOCK lock = SRWLOCK_INIT;
    mapping_pin *head{nullptr};

    ~mapping_registry() {
        while (head) {
            auto *pin = head;
            head = pin->next;
            delete pin;
        }
    }
};

mapping_registry &retained_mappings() {
    static mapping_registry registry;
    return registry;
}

struct mapping_registry_lock {
    SRWLOCK &lock;

    explicit mapping_registry_lock(SRWLOCK &value) noexcept : lock(value) {
        AcquireSRWLockExclusive(&lock);
    }
    ~mapping_registry_lock() {
        ReleaseSRWLockExclusive(&lock);
    }
    mapping_registry_lock(const mapping_registry_lock &) = delete;
    mapping_registry_lock &operator=(const mapping_registry_lock &) = delete;
};

} // namespace

bool shm_handle::valid() const noexcept {
    return handle != nullptr;
}

Result<shm_handle> shm_create(const char *name, std::size_t size, bool *created) {
    if (created) {
        *created = false;
    }
    const char *wname = win_shm_name(name);
    auto &registry = retained_mappings();
    mapping_registry_lock lock(registry.lock);
    DWORD hi = static_cast<DWORD>((size >> 32) & 0xFFFFFFFF);
    DWORD lo = static_cast<DWORD>(size & 0xFFFFFFFF);

    void *h = CreateFileMappingA(
        INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, hi, lo, wname);
    const DWORD create_error = GetLastError();
    if (!h) {
        char buf[128]{};
        std::snprintf(buf, sizeof(buf), "CreateFileMappingA failed: %lu", create_error);
        return Error{ErrorCode::ResourceCreationFailed, buf};
    }

    // Keep exactly one owned pin per name in this library instance. Windows
    // returns a valid handle for an existing mapping, without resizing it.
    for (auto *pin = registry.head; pin; pin = pin->next) {
        if (std::strcmp(pin->name, wname) == 0) {
            if (created) {
                *created = create_error != ERROR_ALREADY_EXISTS;
            }
            return shm_handle{h};
        }
    }

    const auto name_size = std::strlen(wname) + 1;
    auto *pin = new (std::nothrow) mapping_pin;
    if (pin) {
        pin->name = new (std::nothrow) char[name_size];
    }
    if (!pin || !pin->name) {
        delete pin;
        CloseHandle(h);
        return Error{ErrorCode::ResourceCreationFailed, "Cannot retain file mapping name"};
    }
    std::memcpy(pin->name, wname, name_size);
    if (!DuplicateHandle(GetCurrentProcess(), h, GetCurrentProcess(),
                         &pin->handle, 0, FALSE, DUPLICATE_SAME_ACCESS)) {
        DWORD err = GetLastError();
        delete pin;
        CloseHandle(h);
        char buf[128]{};
        std::snprintf(buf, sizeof(buf), "DuplicateHandle failed: %lu", err);
        return Error{ErrorCode::ResourceCreationFailed, buf};
    }
    pin->next = registry.head;
    registry.head = pin;

    if (created) {
        *created = create_error != ERROR_ALREADY_EXISTS;
    }
    return shm_handle{h};
}

Result<shm_handle> shm_open_ro(const char *name) {
    const char *wname = win_shm_name(name);
    void *h = OpenFileMappingA(FILE_MAP_READ, FALSE, wname);
    if (!h) {
        return Error{ErrorCode::SenderNotFound, "OpenFileMappingA failed (read-only)"};
    }
    return shm_handle{h};
}

Result<shm_handle> shm_open_rw(const char *name) {
    const char *wname = win_shm_name(name);
    void *h = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, wname);
    if (!h) {
        return Error{ErrorCode::SenderNotFound, "OpenFileMappingA failed (read-write)"};
    }
    return shm_handle{h};
}

Result<void *> shm_map(shm_handle &h, std::size_t size, bool read_only) {
    DWORD access = read_only ? FILE_MAP_READ : FILE_MAP_ALL_ACCESS;
    void *ptr = MapViewOfFile(h.handle, access, 0, 0, size);
    if (!ptr) {
        char buf[128]{};
        std::snprintf(buf, sizeof(buf), "MapViewOfFile failed: %lu", GetLastError());
        return Error{ErrorCode::ResourceCreationFailed, buf};
    }
    return ptr;
}

void shm_unmap(void *ptr, std::size_t) noexcept {
    if (ptr) {
        UnmapViewOfFile(ptr);
    }
}

void shm_close(shm_handle &h) noexcept {
    if (h.handle) {
        CloseHandle(h.handle);
        h.handle = nullptr;
    }
}

void shm_unlink(const char *name) noexcept {
    const char *wname = win_shm_name(name);
    auto &registry = retained_mappings();
    mapping_registry_lock lock(registry.lock);
    for (auto **entry = &registry.head; *entry; entry = &(*entry)->next) {
        if (std::strcmp((*entry)->name, wname) == 0) {
            auto *pin = *entry;
            *entry = pin->next;
            delete pin;
            return;
        }
    }
}

Result<std::size_t> shm_get_size(const shm_handle &) {
    return Error{ErrorCode::UnsupportedBackend, "shm_get_size not supported on Windows"};
}

bool dir_lock::valid() const noexcept {
    return handle != nullptr;
}

Result<dir_lock> dir_lock_acquire(const char *name, bool) {
    const char *wname = win_shm_name(name);
    char mutex_name[256]{};
    std::snprintf(mutex_name, sizeof(mutex_name), "Global\\nozzle_lock_%s", wname);

    void *h = CreateMutexA(nullptr, FALSE, mutex_name);
    if (!h) {
        char buf[128]{};
        std::snprintf(buf, sizeof(buf), "CreateMutexA failed: %lu", GetLastError());
        return Error{ErrorCode::ResourceCreationFailed, buf};
    }

    DWORD wait = WaitForSingleObject(h, INFINITE);
    // WAIT_ABANDONED grants ownership too; callers must release it normally.
    if (wait != WAIT_OBJECT_0 && wait != WAIT_ABANDONED) {
        CloseHandle(h);
        return Error{ErrorCode::ResourceCreationFailed, "WaitForSingleObject failed"};
    }

    return dir_lock{h};
}

void dir_lock_release(dir_lock &lock) noexcept {
    if (lock.handle) {
        ReleaseMutex(lock.handle);
        CloseHandle(lock.handle);
        lock.handle = nullptr;
    }
}

pid_type current_pid() noexcept {
    return static_cast<pid_type>(GetCurrentProcessId());
}

bool is_pid_alive(pid_type pid) noexcept {
    if (pid == 0) {
        return false;
    }
    void *h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(pid));
    if (!h) {
        return false;
    }
    DWORD exit_code = 0;
    if (GetExitCodeProcess(h, &exit_code) == 0) {
        CloseHandle(h);
        return false;
    }
    CloseHandle(h);
    return exit_code == STILL_ACTIVE;
}

void random_bytes(void *buf, std::size_t len) noexcept {
    BCryptGenRandom(
        nullptr,
        static_cast<PUCHAR>(buf),
        static_cast<ULONG>(len),
        BCRYPT_USE_SYSTEM_PREFERRED_RNG);
}

uint64_t monotonic_ns() noexcept {
    LARGE_INTEGER freq{};
    LARGE_INTEGER counter{};
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&counter);
    const auto ticks = static_cast<uint64_t>(counter.QuadPart);
    const auto ticks_per_sec = static_cast<uint64_t>(freq.QuadPart);
    return (ticks / ticks_per_sec) * 1000000000ULL +
           ((ticks % ticks_per_sec) * 1000000000ULL) / ticks_per_sec;
}

#else

bool shm_handle::valid() const noexcept {
    return fd >= 0;
}

Result<shm_handle> shm_create(const char *name, std::size_t size, bool *created) {
    if (created) {
        *created = false;
    }
    int fd = ::shm_open(name, O_RDWR | O_CREAT | O_EXCL, 0666);
    if (fd < 0) {
        if (errno == EEXIST) {
            fd = ::shm_open(name, O_RDWR, 0);
            if (fd >= 0) {
                return shm_handle{fd};
            }
        }
        return Error{ErrorCode::ResourceCreationFailed, "shm_open failed"};
    }

    if (ftruncate(fd, static_cast<off_t>(size)) != 0) {
        const int saved = errno;
        close(fd);
        ::shm_unlink(name);
        char buf[128]{};
        std::snprintf(buf, sizeof(buf), "ftruncate failed: errno=%d", saved);
        return Error{ErrorCode::ResourceCreationFailed, buf};
    }

    if (created) {
        *created = true;
    }
    return shm_handle{fd};
}

Result<shm_handle> shm_open_ro(const char *name) {
    int fd = shm_open(name, O_RDONLY, 0);
    if (fd < 0) {
        return Error{ErrorCode::SenderNotFound, "shm_open failed (read-only)"};
    }
    return shm_handle{fd};
}

Result<shm_handle> shm_open_rw(const char *name) {
    int fd = shm_open(name, O_RDWR, 0);
    if (fd < 0) {
        return Error{ErrorCode::SenderNotFound, "shm_open failed (read-write)"};
    }
    return shm_handle{fd};
}

Result<void *> shm_map(shm_handle &h, std::size_t size, bool read_only) {
    struct stat st{};
    if (fstat(h.fd, &st) != 0) {
        return Error{ErrorCode::ResourceCreationFailed, "fstat failed before mmap"};
    }
    if (st.st_size < 0 || static_cast<std::uintmax_t>(st.st_size) < size) {
        return Error{ErrorCode::ResourceCreationFailed,
                     "shared memory backing is smaller than requested mapping"};
    }
    // This rejects an already undersized object, not a concurrent truncate.
    int prot = read_only ? PROT_READ : PROT_READ | PROT_WRITE;
    void *ptr = mmap(nullptr, size, prot, MAP_SHARED, h.fd, 0);
    if (ptr == MAP_FAILED) {
        return Error{ErrorCode::ResourceCreationFailed, "mmap failed"};
    }
    return ptr;
}

void shm_unmap(void *ptr, std::size_t size) noexcept {
    if (ptr && ptr != MAP_FAILED) {
        munmap(ptr, size);
    }
}

void shm_close(shm_handle &h) noexcept {
    if (h.fd >= 0) {
        close(h.fd);
        h.fd = -1;
    }
}

void shm_unlink(const char *name) noexcept {
    ::shm_unlink(name);
}

Result<std::size_t> shm_get_size(const shm_handle &h) {
    struct stat st{};
    if (fstat(h.fd, &st) != 0) {
        return Error{ErrorCode::ResourceCreationFailed, "fstat failed"};
    }
    return static_cast<std::size_t>(st.st_size);
}

bool dir_lock::valid() const noexcept {
    return fd >= 0;
}

Result<dir_lock> dir_lock_acquire(const char *name, bool) {
    char lock_path[256]{};
    const char *base = (name[0] == '/') ? name + 1 : name;
    std::snprintf(lock_path, sizeof(lock_path), "/tmp/.nozzle_lock_%s", base);

    int fd = open(lock_path, O_RDWR | O_CREAT, 0666);
    if (fd < 0) {
        return Error{ErrorCode::ResourceCreationFailed, "failed to open lock file"};
    }

    struct flock fl{};
    fl.l_type = F_WRLCK;
    fl.l_whence = SEEK_SET;
    fl.l_start = 0;
    fl.l_len = 0;

    if (fcntl(fd, F_SETLKW, &fl) != 0) {
        close(fd);
        return Error{ErrorCode::ResourceCreationFailed, "fcntl lock failed"};
    }

    return dir_lock{fd, true};
}

void dir_lock_release(dir_lock &lock) noexcept {
    if (lock.fd >= 0 && lock.locked) {
        struct flock fl{};
        fl.l_type = F_UNLCK;
        fl.l_whence = SEEK_SET;
        fl.l_start = 0;
        fl.l_len = 0;
        fcntl(lock.fd, F_SETLK, &fl);
    }
    if (lock.fd >= 0) {
        close(lock.fd);
        lock.fd = -1;
        lock.locked = false;
    }
}

pid_type current_pid() noexcept {
    return static_cast<pid_type>(getpid());
}

bool is_pid_alive(pid_type pid) noexcept {
    if (pid == 0) {
        return false;
    }
    return kill(static_cast<pid_t>(pid), 0) == 0 || errno != ESRCH;
}

void random_bytes(void *buf, std::size_t len) noexcept {
#if NOZZLE_PLATFORM_LINUX
    auto *p = static_cast<unsigned char *>(buf);
    while (len > 0) {
        ssize_t n = getrandom(p, len, 0);
        if (n < 0) {
            if (errno == EINTR) { continue; }
            return;
        }
        p += static_cast<std::size_t>(n);
        len -= static_cast<std::size_t>(n);
    }
#else
    arc4random_buf(buf, len);
#endif
}

uint64_t monotonic_ns() noexcept {
    struct timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1000000000ULL +
           static_cast<uint64_t>(ts.tv_nsec);
}

#endif

} // namespace ipc
} // namespace detail
} // namespace nozzle
