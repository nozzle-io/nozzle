# nozzle

> This codebase is currently in its AI-slob prototyping phase: the code runs on momentum, vibes, and plausible intent.
> Proper debugging will be introduced once demand graduates from hypothetical to measurable.

Cross-platform C/C++17 static library for **local inter-process GPU texture sharing**.

An alternative to Syphon (macOS) and Spout (Windows). Not protocol-compatible with either in v0.1.

## Disclaimer / Notice

This library is currently a work in progress and contains many incomplete features and unverified implementations.
Although it may appear usable at first glance, it may not function correctly.

Please use it with the understanding that no guarantees are made regarding its behavior, and perform debugging, validation, and review as needed.
If you encounter problems, please do not become angry; instead, contributions in the form of Issues or Pull Requests would be greatly appreciated.

## Features

- Named sender/receiver model with discovery
- Metal/IOSurface backend (macOS), D3D11 backend (Windows), DMA-BUF backend (Linux)
- OpenGL interop (copy-based: GL↔IOSurface on macOS, GL↔D3D11 staging on Windows)
- High-precision texture formats (R8, R8G8, RGBA8, R16, RGBA16, R32, R32F, RGBA16F, RGBA32F, etc.)
- C++ API with `Result<T>` error handling (no exceptions)
- C ABI (`nozzle_c.h`) for plugin/host integration
- Zero runtime dependencies (C++17 STL + OS frameworks only)
- Header-only logging via [plog](https://github.com/SergiusTheBest/plog) (submodule)
- Thread-safe sender and receiver objects

## Status

| Component | Status |
|-----------|--------|
| Core API (sender, receiver, frame, texture, device, discovery) | Done |
| macOS Metal/IOSurface backend | Done |
| Windows D3D11 backend | Done |
| Linux DMA-BUF backend | Done |
| OpenGL interop (macOS + Windows) | Done |
| C ABI wrapper | Done |
| Unit tests (67 cases) | Passing |
| Integration tests (27 cases) | Passing |
| py.nozzle (Python, nanobind) | Done (macOS, Windows, Linux) |
| nozzle.swift (Swift, SPM) | Done (macOS) |
| nozzle.rs (Rust, cargo) | Done (macOS, Windows, Linux) |
| ofxNozzle (openFrameworks addon) | Done (macOS, Windows, Linux) |
| jit.nozzle (Max/MSP externals) | Done (macOS, Windows) |
| nozzle-TOP (TouchDesigner plugins) | Done (macOS, Windows) |
| obs-nozzle (OBS Studio plugin) | Done (macOS, Windows, Linux) |
| blender-nozzle (Blender addon) | Done (macOS, Windows, Linux) |
| nozzle.zig (Zig) | Done (macOS, Windows, Linux) |
| nozzle.go (Go, cgo) | Done (macOS, Windows, Linux) |
| nozzle.dart (Dart, FFI) | Done (macOS, Windows, Linux) |
| nozzle.java (Java, JNI) | Done (macOS, Windows, Linux) |
| nozzle.kotlin (Kotlin/JVM, JNI) | Done (macOS, Windows, Linux) |
| nozzle.unity (Unity plugin) | Postponed |
| nozzle.wasm (WebAssembly) | Pended |

## Build

```bash
cmake -B build -DCMAKE_OSX_DEPLOYMENT_TARGET=12.0
cmake --build build
```

### Run Tests

```bash
./build/tests/nozzle_tests --reporter compact
./build/tests/nozzle_integration_tests --reporter compact
```

## Usage

### C++ API

```cpp
#include <nozzle/nozzle.hpp>

// Sender
auto sender = nozzle::sender::create({
    .name = "mySender",
    .application_name = "MyApp",
    .ring_buffer_size = 3
}).value();

// Acquire writable frame, draw into it, commit
auto frame = sender.acquire_writable_frame({
    .width = 1920, .height = 1080,
    .format = nozzle::texture_format::rgba8_unorm
}).value();
// ... write GPU data ...
sender.commit_frame(frame);

// Receiver
auto receiver = nozzle::receiver::create({
    .name = "mySender",
    .application_name = "MyViewer"
}).value();

auto frame = receiver.acquire_frame().value();
auto info = frame.info();
// info.width, info.height, info.frame_index, etc.
```

### C API

```c
#include <nozzle/nozzle_c.h>

NozzleSender *sender;
NozzleSenderDesc desc = {0};
desc.name = "mySender";
desc.application_name = "MyApp";
desc.ring_buffer_size = 3;
nozzle_sender_create(&desc, &sender);

NozzleFrame *frame;
nozzle_sender_acquire_writable_frame(sender, 1920, 1080, NOZZLE_FORMAT_RGBA8_UNORM, &frame);
nozzle_sender_commit_frame(sender, frame);
```

### CPU pixel mapping semantics

New CPU pixel mapping code should use the owned mapping-handle APIs. The handle
owns backend cleanup; the returned pixel view is borrowed and remains valid only
until the handle is unlocked or destroyed.

Preferred C++ writable mapping API:

```cpp
auto mapping_result =
    nozzle::lock_writable_pixels_mapping_with_origin(frame, nozzle::texture_origin::top_left);
if (!mapping_result.ok()) {
    // handle lock error
}

auto mapping = std::move(mapping_result.value());
const nozzle::mapped_pixels &pixels = mapping.pixels();
// write pixels.data

auto unlock_result = mapping.unlock_checked();
if (!unlock_result.ok()) {
    // do not commit after checked unlock failure
}
auto commit_result = sender.commit_frame(frame);
```

Preferred C writable mapping API:

```c
NozzlePixelMapping *mapping = NULL;
NozzleMappedPixels pixels;
NozzleErrorCode rc = nozzle_frame_lock_writable_pixels_mapping_with_origin(
    frame, NOZZLE_ORIGIN_TOP_LEFT, &mapping, &pixels);
if (rc != NOZZLE_OK) {
    /* handle lock error */
}

/* write pixels.data */

rc = nozzle_pixel_mapping_unlock_checked(&mapping);
if (rc != NOZZLE_OK) {
    /* do not commit after checked unlock failure */
}
rc = nozzle_sender_commit_frame(sender, frame);
```

`nozzle_pixel_mapping_unlock_checked(&mapping)` consumes the C mapping handle and
sets `mapping` to `NULL` on success. The unchecked
`nozzle_pixel_mapping_unlock(&mapping)` variant is only for discard-error cleanup.

Read mappings have matching owned-handle APIs:
`lock_frame_pixels_mapping_with_origin(...)` in C++ and
`nozzle_frame_lock_pixels_mapping_with_origin(...)` in C. For readback into caller
memory, `nozzle_frame_copy_pixels_with_origin(...)` remains the safest C ABI path.

The older frame-level CPU mapping APIs are retained as compatibility shims:
`lock_frame_pixels_with_origin(...)` / `unlock_frame_pixels(...)`,
`lock_writable_pixels_with_origin(...)` / `unlock_writable_pixels_checked(...)`,
and their C equivalents. They use one active compatibility mapping slot per
frame or writable frame, while `unlock_frame_pixels(...)` is a no-op when no
legacy read mapping is active and `unlock_writable_pixels_checked(...)` reports
an invalid-argument error when no legacy writable mapping is active. They must
not be treated as the preferred API for new native mapping code.

Required writable order is:

```text
acquire_writable_frame -> lock writable mapping handle -> write pixels -> checked unlock handle -> commit_frame
```

`commit_frame()` publishes an already-prepared writable frame; it is not a
replacement for a successful checked unlock.

For the C API, `nozzle_frame_release(frame)` only releases the frame wrapper. A
writable frame acquired with `nozzle_sender_acquire_writable_frame(...)` must be
committed or discarded before release; releasing it alone does not publish,
discard, or return the reserved sender ring-buffer slot.

## Architecture

```
Layer 4: Integration wrappers (ofxNozzle, jit.nozzle, py.nozzle, nozzle.zig, nozzle.go, nozzle.dart, nozzle.java, nozzle.kotlin, etc.)
Layer 3: OpenGL interop (copy-based GL↔backend)
Layer 2: Backend-native API (Metal/IOSurface, D3D11, DMA-BUF)
Layer 1: Common API (sender, receiver, frame, texture, device, discovery)
Layer 0: Platform infrastructure (registry, IPC, shared memory)
```

### Backend: macOS Metal

Textures are backed by `IOSurface` for cross-process sharing. Sender creates IOSurface-backed Metal textures in a ring buffer. Receiver looks up the IOSurface by ID and creates its own Metal texture view.

ObjC++ code is isolated in `.mm` files. All public headers are pure C++. ARC and non-ARC compatible.

### Backend: Windows D3D11

Textures are created with `D3D11_RESOURCE_MISC_SHARED` for cross-process sharing via `HANDLE`. Sender creates shared textures in a ring buffer. Receiver opens the shared handle and creates its own texture view. Keyed mutex synchronizes access.

### OpenGL Interop

Copy-based path for OpenGL integration. No direct GPU interop (WGL_NV_DX_interop2 is NVIDIA-only).

- **macOS**: GL texture → IOSurface via `CGLTexImageIOSurface2D` + FBO blit
- **Windows**: GL texture → `glGetTexImage` → D3D11 staging texture → `CopySubresourceRegion` (and reverse for receiver)

## Repository Layout

```
include/nozzle/          Public C++ headers
include/nozzle/backends/ Backend-specific headers (metal.hpp, d3d11.hpp, opengl.hpp)
src/common/                  Shared implementation (registry, sender, receiver, frame, etc.)
src/c_api/                   C ABI wrapper
src/backends/metal/          macOS Metal backend (.mm)
src/backends/d3d11/          Windows D3D11 backend (.cpp)
src/backends/opengl/         OpenGL interop (.cpp)
src/backends/linux/          Linux DMA-BUF backend (.cpp)
libs/plog/                   plog submodule (header-only logging)
tests/                       Unit + integration tests (Catch2)
examples/                    Minimal sender/receiver examples
```

## Requirements

- C++17 compiler (no RTTI required)
- macOS 12+ (Metal backend)
- Windows 10+ (D3D11 backend)
- Linux (DMA-BUF backend, glibc 2.31+)
- CMake 3.20+

## Exception Policy

Nozzle does not throw C++ exceptions across API boundaries.

- Public C++ API returns `Result<T>` / `Error`
- C API returns `NozzleErrorCode`
- Backend errors are converted to nozzle errors internally
- Destructors and cleanup paths do not throw

Nozzle does not require `-fno-exceptions` in host applications. If a narrow integration point encounters a throwing API, the exception is caught locally and converted to a nozzle `Error`.

To build nozzle itself with strict `-fno-exceptions` enforcement:

```bash
cmake -B build -DNOZZLE_STRICT_NO_EXCEPTIONS=ON
```

`NOZZLE_STRICT_NO_EXCEPTIONS` is a compile-compatibility profile. In this mode, standard-library failures that would normally be caught and converted to `Error` (e.g. `std::thread` creation, allocation) cannot be intercepted and will terminate the process. Normal host builds should leave exceptions enabled.

## Design Decisions

| Decision | Resolution |
|----------|------------|
| Namespace | `nozzle` |
| Naming | All snake_case in library, `Nozzle` prefix for C API |
| Error handling | `Result<T>` — no exceptions across API boundaries |
| Thread safety | Object-level (same sender/receiver callable from multiple threads) |
| Logging | plog (header-only, submodule) |
| Crash cleanup | Lazy — receiver detects dead sender on access failure |

## Acknowledgments

Nozzle draws significant inspiration from [Syphon](https://syphon.info/) and [Spout](http://spout.zeal.co/). The named sender/receiver model, the concept of shared GPU textures between local processes, and the focus on creative-coding and real-time graphics integration all follow the path they established. While nozzle is an independent implementation with its own API design and is not protocol-compatible with either project, their work directly informed the architecture and goals. Much respect to the Syphon and Spout developers for proving this model works and matters.

## License

MIT

Third-party dependencies:

- [plog](https://github.com/SergiusTheBest/plog) — MIT
