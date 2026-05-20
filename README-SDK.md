# nozzle static SDK

This SDK contains a static nozzle library, public headers, and CMake package files.

## ABI status

nozzle v0.x is pre-ABI-stable. The C ABI in `include/nozzle/nozzle_c.h` is the intended future stable binary boundary, but no binary compatibility is promised until an explicitly ABI-stable release tag says so.

The C++ headers are source-facing only. Do not treat the C++ API or C++ ABI as a stable binary contract.

## Static library link model

Even when an application calls only the C API, the static library is implemented in C++ and platform-native implementation files. Consumers must link with the platform C++ linker/runtime.

Minimal CMake consumer:

```cmake
cmake_minimum_required(VERSION 3.20)
project(nozzle_c_consumer C CXX)

find_package(nozzle CONFIG REQUIRED)

add_executable(nozzle_c_consumer main.c)
set_target_properties(nozzle_c_consumer PROPERTIES LINKER_LANGUAGE CXX)
target_link_libraries(nozzle_c_consumer PRIVATE nozzle::nozzle)
```

Example C source:

```c
#include <stdint.h>
#include <nozzle/nozzle_c.h>

int main(void) {
    NozzleSenderDesc desc = {0};
    uint32_t flags = 0;
    return nozzle_resolve_fallback_flags(&desc, &flags) == NOZZLE_OK ? 0 : 1;
}
```

## Platform dependencies

The installed CMake package is the supported way to consume the SDK. It propagates the platform link dependencies used by the static archive.

- macOS: Metal, IOSurface, Foundation, Accelerate, Objective-C runtime, and OpenGL when OpenGL support is enabled.
- Linux: libdrm, gbm, EGL, and GL when OpenGL support is enabled. Development packages must be available to downstream builds.
- Windows: d3d11, dxgi, bcrypt, and opengl32 when OpenGL support is enabled.

## Windows runtime

The first static SDK uses nozzle's current top-level MSVC static runtime model (`/MT` for Release, `/MTd` for Debug). Consumers that require `/MD` runtime artifacts need a separate SDK policy/artifact variant; that is not implied by this package.
