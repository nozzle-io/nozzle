// nozzle - ipc.cpp - Platform implementations for IPC primitives

#include "ipc.hpp"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <limits>

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
#include <charconv>
#include <sys/random.h>
#elif defined(__APPLE__)
#include <libproc.h>
#endif

#endif

namespace nozzle {
namespace detail {
namespace ipc {

namespace {

#if defined(NOZZLE_ENABLE_TEST_HOOKS) && NOZZLE_ENABLE_TEST_HOOKS
thread_local const process_test_hooks *active_process_test_hooks = nullptr;
#endif

Error native_error(ErrorCode code, const char *operation, uint64_t native_code) {
    return Error{code, std::string(operation) + ": native error " + std::to_string(native_code)};
}

#if defined(_WIN32)
HANDLE open_process_for_probe(DWORD access, pid_type pid) {
#if defined(NOZZLE_ENABLE_TEST_HOOKS) && NOZZLE_ENABLE_TEST_HOOKS
    if (active_process_test_hooks && active_process_test_hooks->native_probe_error) {
        const auto error = active_process_test_hooks->native_probe_error(
            pid, active_process_test_hooks->context);
        if (error) {
            SetLastError(error);
            return nullptr;
        }
    }
#endif
    return OpenProcess(access, FALSE, static_cast<DWORD>(pid));
}
#else
int probe_pid(pid_type pid) {
#if defined(NOZZLE_ENABLE_TEST_HOOKS) && NOZZLE_ENABLE_TEST_HOOKS
    if (active_process_test_hooks && active_process_test_hooks->native_probe_error) {
        const auto error = active_process_test_hooks->native_probe_error(
            pid, active_process_test_hooks->context);
        if (error) {
            errno = static_cast<int>(error);
            return -1;
        }
    }
#endif
    return ::kill(static_cast<pid_t>(pid), 0);
}
#endif

} // namespace

#if defined(NOZZLE_ENABLE_TEST_HOOKS) && NOZZLE_ENABLE_TEST_HOOKS
scoped_process_test_hooks::scoped_process_test_hooks(process_test_hooks hooks) noexcept
    : hooks_{hooks}, previous_{active_process_test_hooks} {
    active_process_test_hooks = &hooks_;
}

scoped_process_test_hooks::~scoped_process_test_hooks() {
    active_process_test_hooks = previous_;
}
#endif

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

Result<void> shm_initialize_empty(shm_handle &, std::size_t) {
    return Error{ErrorCode::UnsupportedBackend,
                 "zero-length shared memory initialization is not supported on Windows"};
}

Result<shm_handle> shm_open_ro(const char *name) {
    const char *wname = win_shm_name(name);
    void *h = OpenFileMappingA(FILE_MAP_READ, FALSE, wname);
    if (!h) {
        const DWORD error = GetLastError();
        return native_error(error == ERROR_FILE_NOT_FOUND ? ErrorCode::SenderNotFound
                                                         : ErrorCode::BackendError,
                            "OpenFileMappingA (read-only)", error);
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

Result<bool> shm_owned_by_current_user(const shm_handle &) {
    return Error{ErrorCode::UnsupportedBackend, "POSIX shared memory ownership is unavailable on Windows"};
}

Result<void> shm_unlink_checked(const char *) {
    return Error{ErrorCode::UnsupportedBackend, "POSIX shared memory unlink is unavailable on Windows"};
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
    void *h = open_process_for_probe(PROCESS_QUERY_LIMITED_INFORMATION, pid);
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

Result<uint64_t> process_instance_token(pid_type pid) {
    if (pid == 0 || pid > (std::numeric_limits<DWORD>::max)()) {
        return Error{ErrorCode::InvalidArgument, "invalid process ID"};
    }
#if defined(NOZZLE_ENABLE_TEST_HOOKS) && NOZZLE_ENABLE_TEST_HOOKS
    if (active_process_test_hooks && active_process_test_hooks->token) {
        return active_process_test_hooks->token(pid, active_process_test_hooks->context);
    }
#endif
    HANDLE process = open_process_for_probe(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, pid);
    if (!process) {
        const DWORD error = GetLastError();
        return native_error(error == ERROR_INVALID_PARAMETER ? ErrorCode::SenderClosed
                                                            : ErrorCode::BackendError,
                            "OpenProcess", error);
    }
    FILETIME creation{}, exit{}, kernel{}, user{};
    const BOOL queried = GetProcessTimes(process, &creation, &exit, &kernel, &user);
    const DWORD query_error = queried ? ERROR_SUCCESS : GetLastError();
    const DWORD wait = WaitForSingleObject(process, 0);
    const DWORD wait_error = wait == WAIT_FAILED ? GetLastError() : ERROR_SUCCESS;
    CloseHandle(process);
    if (wait == WAIT_OBJECT_0) {
        return Error{ErrorCode::SenderClosed, "process has exited"};
    }
    if (wait != WAIT_TIMEOUT) {
        return native_error(ErrorCode::BackendError, "process wait", wait_error);
    }
    if (!queried) {
        return native_error(ErrorCode::BackendError, "GetProcessTimes", query_error);
    }
    const uint64_t token = (static_cast<uint64_t>(creation.dwHighDateTime) << 32)
        | creation.dwLowDateTime;
    if (token == 0) return Error{ErrorCode::BackendError, "process creation time is zero"};
    return token;
}

void random_bytes(void *buf, std::size_t len) noexcept {
    BCryptGenRandom(
        nullptr,
        static_cast<PUCHAR>(buf),
        static_cast<ULONG>(len),
        BCRYPT_USE_SYSTEM_PREFERRED_RNG);
}

uint64_t monotonic_ns() noexcept {
#if defined(NOZZLE_ENABLE_TEST_HOOKS) && NOZZLE_ENABLE_TEST_HOOKS
    if (active_process_test_hooks && active_process_test_hooks->clock_ns) {
        return active_process_test_hooks->clock_ns(active_process_test_hooks->context);
    }
#endif
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

Result<void> shm_initialize_empty(shm_handle &h, std::size_t size) {
    if (size == 0 || static_cast<std::uintmax_t>(size) >
                         static_cast<std::uintmax_t>((std::numeric_limits<off_t>::max)())) {
        return Error{ErrorCode::InvalidArgument, "invalid shared memory initialization size"};
    }

    struct stat st{};
    if (fstat(h.fd, &st) != 0) {
        return Error{ErrorCode::ResourceCreationFailed,
                     "fstat failed before shared memory initialization"};
    }
    if (st.st_size != 0) {
        return Error{ErrorCode::InvalidArgument,
                     "shared memory initialization requires a zero-length object"};
    }

    // The caller's locks exclude cooperating writers between this check and allocation.
    if (ftruncate(h.fd, static_cast<off_t>(size)) != 0) {
        const int saved = errno;
        char buf[128]{};
        std::snprintf(buf, sizeof(buf), "shared memory initialization failed: errno=%d", saved);
        return Error{ErrorCode::ResourceCreationFailed, buf};
    }
    return {};
}

Result<shm_handle> shm_open_ro(const char *name) {
    int fd = shm_open(name, O_RDONLY, 0);
    if (fd < 0) {
        const int error = errno;
        return native_error(error == ENOENT ? ErrorCode::SenderNotFound : ErrorCode::BackendError,
                            "shm_open (read-only)", error);
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

Result<bool> shm_owned_by_current_user(const shm_handle &handle) {
    if (!handle.valid()) return Error{ErrorCode::InvalidArgument, "invalid shared memory handle"};
    struct stat status{};
    if (::fstat(handle.fd, &status) != 0) {
        return native_error(ErrorCode::BackendError, "fstat shared memory ownership", errno);
    }
    return status.st_uid == ::geteuid();
}

Result<void> shm_unlink_checked(const char *name) {
    if (!name || name[0] != '/' || name[1] == '\0' || std::strchr(name + 1, '/')) {
        return Error{ErrorCode::InvalidArgument, "invalid POSIX shared memory name"};
    }
    if (::shm_unlink(name) == 0) return {};
    const int error = errno;
    return native_error(error == ENOENT ? ErrorCode::SenderNotFound : ErrorCode::BackendError,
                        "shm_unlink", error);
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

    int lock_result;
    do {
        lock_result = fcntl(fd, F_SETLKW, &fl);
    } while (lock_result != 0 && errno == EINTR);
    if (lock_result != 0) {
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
    return probe_pid(pid) == 0 || errno != ESRCH;
}

Result<uint64_t> process_instance_token(pid_type pid) {
    if (pid == 0 || pid > static_cast<uint64_t>((std::numeric_limits<pid_t>::max)())) {
        return Error{ErrorCode::InvalidArgument, "invalid process ID"};
    }
#if defined(NOZZLE_ENABLE_TEST_HOOKS) && NOZZLE_ENABLE_TEST_HOOKS
    if (active_process_test_hooks && active_process_test_hooks->token) {
        return active_process_test_hooks->token(pid, active_process_test_hooks->context);
    }
#endif
    const auto native_pid = static_cast<pid_t>(pid);
#if NOZZLE_PLATFORM_LINUX
    char path[64]{};
    std::snprintf(path, sizeof(path), "/proc/%llu/stat", static_cast<unsigned long long>(pid));
    const int fd = ::open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        const int error = errno;
        // A missing/hidden procfs record alone does not prove process death.
        if (error == ENOENT && probe_pid(native_pid) != 0 && errno == ESRCH) {
            return Error{ErrorCode::SenderClosed, "process no longer exists"};
        }
        return native_error(ErrorCode::BackendError, "open process stat", error);
    }
    char buffer[4096]{};
    std::size_t used = 0;
    int read_error = 0;
    while (used < sizeof(buffer) - 1) {
        const ssize_t count = ::read(fd, buffer + used, sizeof(buffer) - 1 - used);
        if (count < 0) {
            if (errno == EINTR) continue;
            read_error = errno;
            break;
        }
        if (count == 0) break;
        used += static_cast<std::size_t>(count);
    }
    ::close(fd);
    if (read_error != 0) return native_error(ErrorCode::BackendError, "read process stat", read_error);
    if (used == 0 || used == sizeof(buffer) - 1 || std::memchr(buffer, '\0', used)) {
        return Error{ErrorCode::BackendError, "incomplete process stat"};
    }
    const char *end = buffer + used;
    uint64_t reported_pid{};
    const auto parsed_pid = std::from_chars(buffer, end, reported_pid);
    if (parsed_pid.ec != std::errc{} || reported_pid != pid || end - parsed_pid.ptr < 3
        || parsed_pid.ptr[0] != ' ' || parsed_pid.ptr[1] != '(') {
        return Error{ErrorCode::BackendError, "invalid process stat PID"};
    }
    // comm is unescaped and can contain spaces, newlines, and parentheses.
    const char *cursor = std::strrchr(buffer, ')');
    if (!cursor || cursor < parsed_pid.ptr + 2 || end - cursor < 4
        || cursor[1] != ' ' || cursor[3] != ' ') {
        return Error{ErrorCode::BackendError, "invalid process stat command/state"};
    }
    cursor += 4; // Field 4 follows the single-character field 3 (state).
    uint64_t token{};
    for (int field = 4; field <= 22; ++field) {
        const char *start = cursor;
        while (cursor < end && *cursor != ' ' && *cursor != '\n') ++cursor;
        const char *digits = start;
        if (field != 22 && digits < cursor && *digits == '-') ++digits;
        uint64_t number{};
        const auto parsed = std::from_chars(digits, cursor, number);
        if (parsed.ec != std::errc{} || parsed.ptr != cursor || cursor == end) {
            return Error{ErrorCode::BackendError, "invalid process stat numeric field"};
        }
        if (field == 22) token = number;
        if (field != 22 && *cursor != ' ') {
            return Error{ErrorCode::BackendError, "truncated process stat fields"};
        }
        ++cursor;
    }
    if (token == 0) return Error{ErrorCode::BackendError, "process start ticks are zero"};
#elif defined(__APPLE__)
    proc_bsdinfo info{};
    errno = 0;
    const int size = ::proc_pidinfo(native_pid, PROC_PIDTBSDINFO, 0, &info, sizeof(info));
    const int error = errno;
    if (size != sizeof(info)) {
        return native_error(size <= 0 && error == ESRCH ? ErrorCode::SenderClosed
                                                       : ErrorCode::BackendError,
                            "proc_pidinfo", error);
    }
    if (info.pbi_pid != pid || info.pbi_start_tvusec >= 1000000
        || info.pbi_start_tvsec > ((std::numeric_limits<uint64_t>::max)()
                                  - info.pbi_start_tvusec) / 1000000) {
        return Error{ErrorCode::BackendError, "invalid process start time"};
    }
    const uint64_t token = info.pbi_start_tvsec * 1000000 + info.pbi_start_tvusec;
    if (token == 0) return Error{ErrorCode::BackendError, "process start time is zero"};
#else
    return Error{ErrorCode::BackendError, "native process start time is unavailable"};
#endif
#if NOZZLE_PLATFORM_LINUX || defined(__APPLE__)
    if (probe_pid(native_pid) != 0) {
        const int error = errno;
        return native_error(error == ESRCH ? ErrorCode::SenderClosed : ErrorCode::BackendError,
                            "process existence check", error);
    }
    return token;
#endif
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
#if defined(NOZZLE_ENABLE_TEST_HOOKS) && NOZZLE_ENABLE_TEST_HOOKS
    if (active_process_test_hooks && active_process_test_hooks->clock_ns) {
        return active_process_test_hooks->clock_ns(active_process_test_hooks->context);
    }
#endif
    struct timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1000000000ULL +
           static_cast<uint64_t>(ts.tv_nsec);
}

#endif

} // namespace ipc
} // namespace detail
} // namespace nozzle
