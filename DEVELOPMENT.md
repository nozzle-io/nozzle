# nozzle — Development Guide

## Prerequisites

- macOS 12+ with Xcode Command Line Tools
- CMake 3.20+
- Git (with submodule support)

## Quick Start

```bash
git clone --recurse-submodules git@github.com:2bbb/nozzle.git
cd nozzle
cmake -B build -DCMAKE_OSX_DEPLOYMENT_TARGET=12.0
cmake --build build
```

### Run Tests

```bash
./build/tests/nozzle_tests --reporter compact
./build/tests/nozzle_integration_tests --reporter compact
```

### Run Examples

Two terminal windows:

```bash
# Terminal 1: sender
./build/examples/example_sender

# Terminal 2: receiver
./build/examples/example_receiver
```

## Build Configuration

### CMake Options

| Option | Default | Description |
|--------|---------|-------------|
| `NOZZLE_BUILD_TESTS` | ON | Build unit/integration tests |
| `NOZZLE_BUILD_EXAMPLES` | ON | Build example programs |
| `CMAKE_OSX_DEPLOYMENT_TARGET` | 12.0 | Minimum macOS version |
| `CMAKE_OSX_ARCHITECTURES` | Host arch | Set `"arm64;x86_64"` for universal |

### Universal Binary

```bash
cmake -B build -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
cmake --build build
```

### Release Build

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

## Codebase Structure

```
nozzle/
├── include/bbb/nozzle/           # Public C++ API headers
│   ├── nozzle.hpp                #     Umbrella include
│   ├── nozzle_c.h                #     C ABI header
│   ├── types.hpp                 #     Enums, structs, descriptors
│   ├── result.hpp                #     Result<T> error handling
│   ├── sender.hpp                #     sender class
│   ├── receiver.hpp              #     receiver class
│   ├── frame.hpp                 #     frame / writable_frame
│   ├── texture.hpp               #     texture class
│   ├── device.hpp                #     device class
│   ├── discovery.hpp             #     sender discovery
│   └── backends/
│       ├── metal.hpp             #     Metal backend API
│       └── d3d11.hpp             #     D3D11 backend API (stubs)
├── src/
│   ├── common/                   # Platform-independent implementation
│   │   ├── registry.cpp/hpp      #     Shared memory registry (shm_open/mmap)
│   │   ├── shared_state.hpp      #     Shared memory layout structs
│   │   ├── sender.cpp            #     sender implementation
│   │   ├── receiver.cpp          #     receiver implementation
│   │   ├── frame.cpp             #     frame lifetime management
│   │   ├── frame_helpers.hpp     #     frame construction helpers (detail)
│   │   ├── texture.cpp           #     texture implementation
│   │   ├── device.cpp            #     device implementation
│   │   ├── discovery.cpp         #     sender enumeration
│   │   ├── metadata.cpp/hpp      #     key=value metadata serialize/parse
│   │   └── metal_helpers.hpp     #     Metal texture helpers (detail)
│   ├── c_api/
│   │   └── nozzle_c.cpp          # C ABI wrapper (thin wrapper over C++ API)
│   └── backends/
│       ├── metal/                 # macOS Metal backend (.mm — ObjC++)
│       │   ├── metal_backend.mm  #     Device management
│       │   ├── metal_texture.mm  #     IOSurface texture creation/lookup
│       │   └── metal_sync.mm     #     IOSurface lock-based synchronization
│       └── d3d11/                 # Windows D3D11 backend (stubs)
│           ├── d3d11_backend.cpp
│           ├── d3d11_texture.cpp
│           └── d3d11_sync.cpp
├── libs/plog/                    # plog submodule (header-only logging)
├── tests/
│   ├── unit/                     # Unit tests (Catch2)
│   └── integration/              # Cross-process integration tests
├── examples/
│   ├── example_sender.cpp
│   └── example_receiver.cpp
├── PLAN.md                       # Full design document (22 sections)
└── AGENTS.md                     # AI agent context
```

## Architecture Deep Dive

### Layer 0: IPC / Registry

Cross-process communication uses POSIX shared memory (`shm_open` / `mmap`):

- **Directory** (`/nozzle_dir`): Fixed-size shared memory containing up to 64 `DirectoryEntry` structs. Each entry maps a sender name to its per-sender shared memory segment.
- **Sender state** (`/nozzle_<uuid>`): Per-sender shared memory containing `SenderSharedState` — ring buffer slot info, committed frame counter, metadata, and the sender's PID for crash detection.

Synchronization uses `_Atomic` fields with `__atomic_thread_fence` for visibility across processes. No mutexes in shared memory.

### Layer 1: Common API

- **`sender`**: Creates a ring buffer of IOSurface-backed Metal textures. `acquire_writable_frame()` returns a slot for rendering. `commit_frame()` publishes it. `publish_external_texture()` publishes a user-owned texture.
- **`receiver`**: Opens sender's shared state, reads committed slot, looks up IOSurface by ID via `IOSurfaceLookup`. `acquire_frame()` returns a read-only frame view.
- **`frame` / `writable_frame`**: RAII wrappers around textures. Frames are automatically returned to the ring buffer on destruction or explicit `release()`.
- **`texture`**: Holds backend-native handles (Metal texture + IOSurface). Opaque to users unless they access backend-specific APIs.
- **`device`**: Wraps a Metal device (`id<MTLDevice>`). Can be auto-detected or user-provided via `metal::wrap_device()`.
- **`discovery`**: Reads the directory shared memory to enumerate active senders.

### Layer 2: Metal Backend

All ObjC++ code lives in `.mm` files under `src/backends/metal/`. Headers are pure C++ — ObjC types are stored as `void*` and cast internally.

Key functions:
- `metal::create_iosurface_texture()` — Creates an IOSurface + Metal texture pair
- `metal::lookup_iosurface_texture()` — Creates a Metal texture view of an existing IOSurface (by ID)
- `metal::wrap_texture()` — Wraps an existing Metal texture + IOSurface into a nozzle texture
- `metal::get_io_surface()` — Extracts the IOSurface from a nozzle texture

**ARC compatibility**: All `.mm` files compile under both ARC and non-ARC via `__has_feature(objc_arc)` bridging macros (`NOZZLE_BRIDGE_GET`, `NOZZLE_BRIDGE_RETAIN`, `NOZZLE_BRIDGE_RELEASE`, `NOZZLE_RETAIN`).

### Layer 3: OpenGL Interop

Planned. Will provide copy-based paths:
- macOS: GL → IOSurface via `CGLTexImageIOSurface2D`
- Windows: GL ↔ D3D11 via WGL_NV_DX_interop or copy

### Ring Buffer Flow

```
Sender:
  1. acquire_writable_frame(desc)
     → picks next ring slot, returns writable_frame
  2. User renders into the texture
  3. commit_frame(frame)
     → atomically updates committed_frame + committed_slot
     → frame is now visible to receivers

Receiver:
  1. acquire_frame()
     → reads committed_slot from shared state
     → IOSurfaceLookup(iosurface_id) → Metal texture
     → returns frame (read-only)
  2. User reads texture data
  3. frame destructor returns resources
```

Ring buffer size is configurable per sender (default: 3). Committed slots use `_Atomic uint64_t` for lock-free reads.

## Coding Conventions

### Naming

ALL snake_case in the nozzle library. No camelCase.

```cpp
// Types
sender_info, texture_desc, frame_info, backend_type

// Members
application_name, frame_index, timestamp_ns

// Functions
acquire_frame(), create_iosurface_texture()

// Variables
state_fd, frame_counter, native_device

// Files
metal_texture.mm, shared_state.hpp
```

Exception: C API uses `Nozzle` prefix for types and `nozzle_` prefix for functions.

### C++ Rules

- C++17, no exceptions, no RTTI (in CMake builds)
- `Result<T>` for all fallible operations — always check return values
- `{}` brace initialization preferred
- `*` and `&` bind to type side: `const texture &tex`, `void *ptr`
- `#pragma once` for include guards
- No `using namespace` directives
- Namespace-closing comments: `} // namespace nozzle`
- No unnecessary comments or docstrings

### Include Order

```
1. Corresponding header
2. C++ standard library
3. OS frameworks
4. Nozzle headers
```

### Objective-C++ (.mm files)

- ObjC++ only in `.mm` files — never in `.cpp` or public headers
- All ObjC types stored as `void*` in headers, cast internally
- Use `@autoreleasepool` for all ObjC code
- ARC/non-ARC compatible via `NOZZLE_BRIDGE_*` macros
- Use pimpl to hide ObjC from C++ headers

## Testing

### Unit Tests

Located in `tests/unit/`. Tests individual components in-process using mock registry.

```bash
./build/tests/nozzle_tests          # Run all
./build/tests/nozzle_tests [registry]  # Run specific test case
```

### Integration Tests

Located in `tests/integration/`. Tests cross-process communication by forking sender/receiver processes.

```bash
./build/tests/nozzle_integration_tests
```

### Test Framework

[Catch2](https://github.com/catchorg/Catch2) v3. Fetched via CMake FetchContent.

## Adding a New Backend

1. Create `include/bbb/nozzle/backends/<backend>.hpp` with guard `#if NOZZLE_HAS_<BACKEND>`
2. Create `src/backends/<backend>/` directory with implementation files
3. Add `NOZZLE_HAS_<BACKEND>` detection to root `CMakeLists.txt`
4. Implement required backend operations (texture create/lookup, device management, sync)
5. Add platform to `AGENTS.md` coding rules

## Integration Projects

### ofxNozzle (openFrameworks addon)

Repo: `git@github.com:2bbb/ofxNozzle.git`

- nozzle included as git submodule at `libs/nozzle/`
- Compiles nozzle sources directly via `addon_config.mk` (no prebuilt lib)
- `.mm` files compile under oF's ARC-enabled build
- `addon_config.mk` specifies sources, includes, frameworks, and excludes

### bbb.nozzle (Max external)

Repo: `git@github.com:2bbb/bbb.nozzle.git`

- Uses nozzle's C ABI (`nozzle_c.h`)
- Built with min-api + max-sdk-base (submodules)
- Outputs universal binary `.mxo` externals (x86_64 + arm64)
- Build: `cmake -B build && cmake --build build`

## Common Issues

### Build error: `Metal backend not available on this platform`

`NOZZLE_HAS_METAL` is not defined. When building without CMake (e.g. embedding in another build system), you must define it manually via `-DNOZZLE_HAS_METAL`.

### Build error: ARC forbids explicit retain/release

Fixed in current codebase. If backporting, use the `NOZZLE_BRIDGE_*` macros from `metal_texture.mm`.

### Linker error: undefined symbols for x86_64

nozzle was built for arm64 only. Rebuild with `-DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"`.

### Shared memory leftover after crash

Stale `/nozzle_dir` and `/nozzle_*` shm segments may persist. The library does lazy cleanup on `receiver::acquire_frame()` when it detects a dead sender (via PID check). Manual cleanup: `ls /dev/shm/nozzle_*` on Linux or reboot on macOS.

## Useful Files

| File | Purpose |
|------|---------|
| `PLAN.md` | Complete design document (22 sections) — canonical reference |
| `AGENTS.md` | AI agent context and coding rules |
| `src/common/shared_state.hpp` | Shared memory layout (ring buffer, slots, atomics) |
| `src/common/registry.hpp` | Registry API (register/unregister/lookup senders) |
| `src/backends/metal/metal_helpers.hpp` | Internal texture construction helpers |
| `include/bbb/nozzle/result.hpp` | Result<T> implementation |
