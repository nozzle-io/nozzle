# nozzle

Cross-platform C/C++17 static library for **local inter-process GPU texture sharing**.

An alternative to Syphon (macOS) and Spout (Windows). Not protocol-compatible with either in v0.1.

## Features

- Named sender/receiver model with discovery
- Metal/IOSurface backend (macOS), D3D11 backend planned (Windows)
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
| macOS Metal backend | Done |
| Windows D3D11 backend | Stubs only |
| C ABI wrapper | Done |
| Unit tests (11 cases, 60 assertions) | Passing |
| Integration tests (9 cases, 59 assertions) | Passing |
| openFrameworks addon (ofxNozzle) | In progress |
| Max external (bbb.nozzle) | Built (universal binary) |

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
#include <bbb/nozzle/nozzle.hpp>

// Sender
auto sender = bbb::nozzle::sender::create({
    .name = "mySender",
    .application_name = "MyApp",
    .ring_buffer_size = 3
}).value();

// Acquire writable frame, draw into it, commit
auto frame = sender.acquire_writable_frame({
    .width = 1920, .height = 1080,
    .format = bbb::nozzle::texture_format::rgba8_unorm
}).value();
// ... write GPU data ...
sender.commit_frame(frame);

// Receiver
auto receiver = bbb::nozzle::receiver::create({
    .name = "mySender",
    .application_name = "MyViewer"
}).value();

auto frame = receiver.acquire_frame().value();
auto info = frame.info();
// info.width, info.height, info.frame_index, etc.
```

### C API

```c
#include <bbb/nozzle/nozzle_c.h>

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
Layer 4: Integration wrappers (ofxNozzle, bbb.nozzle Max external)
Layer 3: OpenGL interop (planned)
Layer 2: Backend-native API (Metal/IOSurface, D3D11)
Layer 1: Common API (sender, receiver, frame, texture, device, discovery)
Layer 0: Platform infrastructure (registry, IPC, shared memory)
```

### Backend: macOS Metal

Textures are backed by `IOSurface` for cross-process sharing. Sender creates IOSurface-backed Metal textures in a ring buffer. Receiver looks up the IOSurface by ID and creates its own Metal texture view.

ObjC++ code is isolated in `.mm` files. All public headers are pure C++. ARC and non-ARC compatible.

## Repository Layout

```
include/bbb/nozzle/          Public C++ headers
include/bbb/nozzle/backends/ Backend-specific headers (metal.hpp)
src/common/                  Shared implementation (registry, sender, receiver, frame, etc.)
src/c_api/                   C ABI wrapper
src/backends/metal/          macOS Metal backend (.mm)
src/backends/d3d11/          Windows D3D11 backend (stubs)
libs/plog/                   plog submodule (header-only logging)
tests/                       Unit + integration tests (Catch2)
examples/                    Minimal sender/receiver examples
```

## Requirements

- C++17 compiler (no exceptions, no RTTI required)
- macOS 12+ (Metal backend)
- Windows 10+ (D3D11 backend, planned)
- CMake 3.20+

## Design Decisions

| Decision | Resolution |
|----------|------------|
| Namespace | `bbb::nozzle` |
| Naming | All snake_case in library, `Nozzle` prefix for C API |
| Error handling | `Result<T>` — no exceptions |
| Thread safety | Object-level (same sender/receiver callable from multiple threads) |
| Logging | plog (header-only, submodule) |
| Crash cleanup | Lazy — receiver detects dead sender on access failure |

## License

MIT
