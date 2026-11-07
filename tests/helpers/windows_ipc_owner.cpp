#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

int main(int argc, char **argv) {
    if (argc != 2) {
        return 2;
    }
    HANDLE mutex = OpenMutexA(SYNCHRONIZE | MUTEX_MODIFY_STATE, FALSE, argv[1]);
    if (!mutex) {
        return 3;
    }
    const DWORD wait = WaitForSingleObject(mutex, 5000);
    if (wait != WAIT_OBJECT_0) {
        if (wait == WAIT_ABANDONED) {
            ReleaseMutex(mutex);
        }
        CloseHandle(mutex);
        return 4;
    }
    // Deliberately exit owning the mutex; the parent keeps the object alive.
    // Process teardown closes this handle without releasing mutex ownership.
    ExitProcess(0);
}
