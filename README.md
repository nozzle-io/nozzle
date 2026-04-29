# nozzle

Cross-platform C/C++17 static library for **local inter-process GPU texture sharing**.

An alternative to Syphon (macOS) and Spout (Windows). Not protocol-compatible with either in v0.1.

## Features

- Named sender/receiver model with discovery
- Metal/IOSurface backend (macOS), D3D11 backend (Windows), DMA-BUF backend (Linux)
- OpenGL interop (copy-based: GL↔IOSurface on macOS, GL↔D3D11 staging on Windows)
- High-precision texture formats (R32F, RGBA16F, RGBA32F, etc.)
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
| Unit tests (11 cases) | Passing |
| Integration tests (9 cases) | Passing |
| py.nozzle (Python, nanobind) | Done (macOS, Windows, Linux) |
| nozzle.swift (Swift, SPM) | Done (macOS) |
| nozzle.rs (Rust, cargo) | Done (macOS, Windows, Linux) |
| ofxNozzle (openFrameworks addon) | Done (macOS, Windows, Linux) |
| nozzle.max (Max/MSP externals) | Done (macOS, Windows) |
| nozzle-TOP (TouchDesigner plugins) | Done (macOS, Windows) |
| obs-nozzle (OBS Studio plugin) | Done (macOS, Windows, Linux) |
| blender-nozzle (Blender addon) | Done (macOS, Windows, Linux) |
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

## Architecture

```
Layer 4: Integration wrappers (ofxNozzle, nozzle.max, py.nozzle, etc.)
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

- C++17 compiler (no exceptions, no RTTI required)
- macOS 12+ (Metal backend)
- Windows 10+ (D3D11 backend)
- Linux (DMA-BUF backend, glibc 2.31+)
- CMake 3.20+

## Design Decisions

| Decision | Resolution |
|----------|------------|
| Namespace | `nozzle` |
| Naming | All snake_case in library, `Nozzle` prefix for C API |
| Error handling | `Result<T>` — no exceptions |
| Thread safety | Object-level (same sender/receiver callable from multiple threads) |
| Logging | plog (header-only, submodule) |
| Crash cleanup | Lazy — receiver detects dead sender on access failure |

## Acknowledgments

Nozzle draws significant inspiration from [Syphon](https://syphon.info/) and [Spout](http://spout.zeal.co/). The named sender/receiver model, the concept of shared GPU textures between local processes, and the focus on creative-coding and real-time graphics integration all follow the path they established. While nozzle is an independent implementation with its own API design and is not protocol-compatible with either project, their work directly informed the architecture and goals. Much respect to the Syphon and Spout developers for proving this model works and matters.

## License

MIT
