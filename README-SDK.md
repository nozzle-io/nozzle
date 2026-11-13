# nozzle static SDK

This SDK contains a static nozzle library, public headers, and CMake package files.

## ABI status

nozzle v0.x is pre-ABI-stable. The C ABI in `include/nozzle/nozzle_c.h` is the intended future stable binary boundary, but no binary compatibility is promised until an explicitly ABI-stable release tag says so.

The C++ headers are source-facing only. Do not treat the C++ API or C++ ABI as a stable binary contract.

## Embedding in plugins

Core's CMake target compiles static archive symbols with hidden visibility. A
plugin embedding this archive must export its own host entry points, not re-export
the static core's C/C++ symbols. Otherwise a host loading multiple core versions
with `RTLD_GLOBAL` can bind one plugin's calls to another plugin's implementation.
Hiding only the plugin's own source does not hide objects in a linked archive.

For Linux builds that compile core outside CMake, use hidden visibility for core
objects or localize the archive at the final DSO link, for example
`-Wl,--exclude-libs,libnozzle.a`. This ELF option must not be applied on macOS or
Windows. Preserve the wrapper's required host exports. A wrapper intentionally
providing a shared nozzle C API must instead use the shared-library build and its
`NOZZLE_EXPORTS`/`NOZZLE_SHARED` contract, not rely on incidental static exports.

Linux DMA-BUF sender state is owned by a linked core image. Within that image the
backend still supports one sender; isolated static plugin images have separate
state, whereas clients of the same shared core image share it. This isolation
change does not add multi-sender support within an image or change the published
capability structure. It also does not establish host-specific GPU interoperability.

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

## Linux x64 ISA baseline

The Linux x64 SDK archive is built with `-march=x86-64-v2`. Its CI checks it for AVX, AVX2, FMA and F16C instructions with `scripts/check_x86_64_isa.sh` from the nozzle source repository; that script is not included in this archive. The ISA of your own code is set by your build flags.

## Windows runtime

The first static SDK uses nozzle's current top-level MSVC static runtime model (`/MT` for Release, `/MTd` for Debug). Consumers that require `/MD` runtime artifacts need a separate SDK policy/artifact variant; that is not implied by this package.
