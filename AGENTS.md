# Nozzle - Project AGENTS.md

## Project Status

**Phase 0 in progress.** Layer 0 (IPC/registry) and Layer 1 (sender/receiver/frame/texture/device) implemented. Metal backend functional. Tests passing (11 test cases). `PLAN.md` is the canonical design document.

## What This Is

Cross-platform C/C++17 static library for **local inter-process GPU texture sharing**. Alternative to Syphon (macOS) and Spout (Windows), but not protocol-compatible with either in v0.1.

## Key Decisions (Resolved)

These are locked in. Do not re-debate:

| Decision | Resolution |
|---|---|
| Min OS | macOS 12+, Windows 10+ |
| C++ standard | C++17, no exceptions |
| Error handling | `Result<T>` pattern (no exceptions, no `std::optional` error info) |
| Thread safety | Object-level thread-safe (same Sender/Receiver callable from multiple threads) |
| Device model | Default device auto-detection + explicit `wrapDevice()` — support both |
| OpenGL support | **Core v0.1 feature** (not "experimental helper"). Required for ofx and Max integration |
| Crash cleanup | Lazy detection — receiver detects dead sender on access failure |
| Integration targets | openFrameworks addon + Max external — both simultaneously |
| Dependencies | Zero runtime deps. C++17 STL + OS frameworks only. Header-only libs allowed if justified |
| Logging | Header-only log library allowed. Find one, add it |
| Testing | Test thoroughly. Unit tests are the baseline requirement, not the ceiling. Integration tests (cross-process, AI-friendly) are expected. More coverage is always better. |
| Build system | CMake static library |
| Distribution | Static lib + source-integration friendly layout |

## Architecture (4 Layers)

```
Layer 4: Integration wrappers (ofx addon, Max external)
Layer 3: OpenGL interop (core feature, not helper)
Layer 2: Backend-native API (D3D11, Metal/IOSurface)
Layer 1: Common API (Sender, Receiver, Frame, Texture, Device, Discovery)
Layer 0: Platform infrastructure (Registry, IPC, shared memory, sync)
```

## Backend Strategy

- **macOS**: Metal textures backed by IOSurface. Objective-C++ internally (`.mm` files).
- **Windows**: D3D11 shared textures with `HANDLE` sharing. COM internally.
- **OpenGL**: Copy-based path (GL → Metal/IOSurface on macOS, GL ↔ D3D11 on Windows).

## Critical Path for Phase 0

These must be resolved before any Layer 1+ code:

1. **IPC/Registry mechanism** — How senders announce themselves, how receivers discover. Not decided yet. Consult Oracle with full PLAN.md context when starting Phase 0.
2. **Ring buffer synchronization** — Sender-internal mutex + Receiver-internal mutex + shared atomic state? Needs investigation.
3. **D3D11 keyed mutex strategy** — What sync primitive for shared texture access.
4. **Metal/IOSurface visibility model** — Command buffer completion + IOSurface lock semantics.

## Additional Resolved Decisions (Phase 0)

| Decision | Resolution |
|---|---|
| Namespace | `bbb::nozzle` (bbb is the author's library prefix) |
| applicationName | User-provided. Auto-detection optional if easy. |
| timestampNs | Host time (`clock_gettime(CLOCK_MONOTONIC)` / `QueryPerformanceCounter`) |
| Format compatibility | Warning + fallback to nearest compatible format when receiver device differs |

## API Conventions

- `bbb::nozzle::` namespace for C++ API (class names are `snake_case` inside `bbb` namespace)
- `Nozzle*` opaque handles for C ABI
- `NOZZLE_PLATFORM_*` / `NOZZLE_HAS_*` macros for conditional compilation
- Backend-specific code guarded: `#if NOZZLE_HAS_D3D11`, `#if NOZZLE_HAS_METAL`
- RAII for frame lifetime. Explicit `release()` also available.
- `TransferMode` enum exposed on frames — users can check if zero-copy or copy-based

## Repository Layout

```
include/bbb/nozzle/          # Public headers (nozzle.hpp, sender.hpp, receiver.hpp, etc.)
include/bbb/nozzle/backends/ # Backend-specific headers (d3d11.hpp, metal.hpp)
src/common/                  # Shared implementation (registry, sender, receiver, frame)
src/c_api/                   # C ABI wrapper
src/backends/d3d11/          # Windows D3D11 (.cpp)
src/backends/metal/          # macOS Metal (.mm)
tests/                       # Unit + integration tests
```

## Development Environment

- Primary dev on macOS (Apple Silicon). Build with Xcode clang.
- Windows builds via GitHub Actions CI. Physical Windows testing available at later stage.
- CMake is the build system. Configure with `-DCMAKE_OSX_DEPLOYMENT_TARGET=12.0`.

## Coding Rules

- **Naming convention: ALL snake_case.** No camelCase anywhere in the nozzle library.
  - Types: `sender_info`, `texture_desc`, `frame_info`, `backend_type`, etc.
  - Struct members: `application_name`, `frame_index`, `timestamp_ns`, `timeout_ms`, etc.
  - Enum values: `r8_unorm`, `rgba8_srgb`, `gpu_copy`, `shader_read`, etc.
  - Functions/methods: `acquire_frame()`, `create_iosurface_texture()`, etc.
  - Variables: `state_fd`, `frame_counter`, `native_device`, etc.
  - Files: `metal_texture.mm`, `shared_state.hpp`, etc.
  - C API types use `Nozzle` prefix: `NozzleSender`, `NozzleReceiver` (C convention)
  - C API functions use snake_case: `nozzle_sender_create`, `nozzle_receiver_acquire_frame`
- C++ coding conventions: follow `~/.agents/docs/C++.md`
- openFrameworks conventions: follow `~/.agents/docs/openFrameworks.md` (for Layer 4 integration)
- No `as any`, `#pragma once` is fine, no exceptions
- Objective-C++ only in `.mm` files, never in `.cpp` or headers
- Include order: corresponding header → C++ std → OS frameworks → nozzle headers
- `Result<T>` must always be checked — never ignore return values in examples/tests
- Namespace: `bbb::nozzle` for C++ API
- No `using namespace` directives
- `{}` brace initialization preferred
- `*` and `&` bind to the type side: `const texture &tex`, `void *ptr`
- Namespace-closing comments required: `} // namespace nozzle` etc.
- No unnecessary comments. Code should be self-documenting.
- No docstrings. No file-level block comments beyond the one-line identifier.

## Plan Document

`PLAN.md` contains 22 sections covering everything from API sketches to test strategy. Sections 1-20 are the design. Section 21 lists Open Questions. Section 22 is the executive summary.

**When in doubt, PLAN.md wins.** If code contradicts PLAN.md, flag it.
