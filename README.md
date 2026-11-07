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

The API/ABI may change in v0.x; the `stable` branch does not mean the API is frozen.

### Known limitations

- **D3D11 ([#187](https://github.com/nozzle-io/nozzle-dev/issues/187)):** frames are not broadcast to every receiver. In the two-receiver CI measurement, B could not acquire the frame A had released; default `timeout_ms = 0` waited about 1.00 s, and commit onto a held slot waited about 1.00 s, for both ring sizes 1 and 3. The synchronization fix is pending.
- **Names ([#252](https://github.com/nozzle-io/nozzle-dev/issues/252)):** JNI wrappers reject names containing non-BMP characters because they pass modified UTF-8. Windows programs using narrow `argv` can reject non-ASCII names when the bytes are not UTF-8. Use ASCII names through these paths until the wrappers convert correctly.

## Features

- Named sender/receiver model with discovery
- Metal/IOSurface backend (macOS), D3D11 backend (Windows), DMA-BUF backend (Linux)
- OpenGL interop (copy-based: GL↔IOSurface on macOS, GL↔D3D11 staging on Windows)
- High-precision texture formats (R8, R8G8, RGBA8, R16, RGBA16, R32, R32F, RGBA16F, RGBA32F, etc.)
- C++ API with `Result<T>` error handling (no exceptions)
- C ABI (`nozzle_c.h`) for plugin/host integration
- Zero runtime dependencies (C++17 STL + OS frameworks only)
- Header-only logging via [plog](https://github.com/SergiusTheBest/plog) (submodule)
- Thread-safe sender and receiver objects, except the deprecated C borrowed-pointer
  info getters, which require external synchronization; use the value snapshot
  getters for concurrent access

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
// Metal: wait for successful completion of ALL GPU writes before publishing.
// Merely submitting commands is insufficient; see the Metal example below.
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
/* Prepare the pixels. On Metal, complete all GPU writes before this call. */
nozzle_sender_commit_frame(sender, frame);
```

### Metal writable-frame completion

For GPU writes into a frame returned by `acquire_writable_frame`, the caller must
ensure **all writes have successfully completed before `commit_frame`** (also
`nozzle_sender_commit_frame` in C). Publishing makes the frame discoverable; it
does not submit a command buffer, wait for GPU completion, or communicate a GPU
fence to receivers. `acquire_frame` does not insert a Metal GPU wait either.

Here is a synchronous Objective-C++ (`.mm`) example. The command buffer must be
newly encoded, not yet committed, and cover every producer write to this frame;
any writes from other command buffers or queues must also finish first.

```objective-c++
#import <Metal/Metal.h>
#include <nozzle/nozzle.hpp>

nozzle::Result<void> finish_and_publish(
    nozzle::sender &sender, nozzle::writable_frame &frame,
    id<MTLCommandBuffer> command_buffer) {
    if (command_buffer) {
        [command_buffer commit];
        [command_buffer waitUntilCompleted];
    }
    if (!command_buffer ||
        command_buffer.status != MTLCommandBufferStatusCompleted) {
        auto discarded = sender.discard_frame(frame);
        if (!discarded.ok()) return discarded.error();
        return nozzle::Error{nozzle::ErrorCode::BackendError,
                             "Metal producer commands did not complete successfully"};
    }
    return sender.commit_frame(frame);
}
```

[`waitUntilCompleted`](https://developer.apple.com/documentation/metal/mtlcommandbuffer/waituntilcompleted())
blocks the calling thread. Command submission or `waitUntilScheduled` alone is
not completion. An asynchronous integration can instead publish after successful
completion notification for all producer writes, keeping the sender and writable
frame alive and reserved until then. Do not discard or recycle a frame while GPU
work may still be writing it. CPU-mapped writes still require a successful
checked unlock before publishing; an IOSurface CPU lock is not a GPU fence.

Writable acquisition scans from the next ring slot, and successful commits
advance that slot; it is **not fixed to slot 0**. Ring rotation is not receiver
completion tracking: retaining an acquired frame keeps its resource alive but
does not freeze its pixels or prevent a later producer write on slot reuse.
This producer-completion contract does not supply consumer acknowledgments,
immutable snapshots, or cross-process Metal GPU synchronization. Integrations
needing stable contents throughout consumer GPU work require additional
coordination; a larger ring alone is not such a guarantee.

### Names and UTF-8

Sender identity (`name`) is **1 to 63 bytes of valid UTF-8**, excluding the
terminating NUL. Both sender creation and receiver lookup reject longer names
or invalid UTF-8 with `InvalidArgument` (`NOZZLE_ERROR_INVALID_ARGUMENT` in C).
Identity is never truncated. C++ strings must not contain embedded NUL bytes;
C arguments must be NUL-terminated.

`application_name` is display text. Sender creation validates the entire input,
then stores at most 63 bytes, ending on a UTF-8 code-point boundary. An omitted
or empty value becomes `unknown`. Discovery, sender info, receiver info and C
snapshots all expose that stored value. Code-point-safe truncation does not
preserve grapheme clusters: combining marks or ZWJ emoji sequences can be split.

**Breaking change (nozzle-dev#190):** applications that previously created
senders with names longer than 63 bytes or malformed UTF-8 now receive an error
at creation. Pass standard UTF-8 at wrapper boundaries. Windows ANSI/CP932
strings and JNI modified UTF-8 are not substitutes; convert from the host's
encoding before calling nozzle. No shared-memory capacity, layout or version
changes are part of this validation change.

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
Layer 3: OpenGL interop (GPU interop where available, explicit copy fallback)
Layer 2: Backend-native API (Metal/IOSurface, D3D11, DMA-BUF)
Layer 1: Common API (sender, receiver, frame, texture, device, discovery)
Layer 0: Platform infrastructure (registry, IPC, shared memory)
```

### Shared-memory compatibility

Directory readers and writers validate magic/version, capacity, and identity
fields before using them as bounds or strings. Receivers additionally reject
invalid sender-state headers, ring sizes, and committed slots. Existing initialized
or foreign mappings are not resized, reset, or replaced to fit the current layout.
Under the directory lock, a writer can recover an empty POSIX object left before
allocation, or a full-size unpublished directory containing only zero entries
and an unfinished supported header. Other corruption is rejected, not repaired.
POSIX mappings check the backing
extent reported by `fstat` before access; Windows requests the full view extent.
These checks validate OS-accessible backing, not the creator's original byte
count. macOS reports page-rounded shared-memory lengths, and Windows page-backed
sections can also cover a request beyond the original size within that page.
A 1663-byte creation request can therefore cover the 1664-byte sender layout on
those systems. Version and field validation still apply within the accessible
mapping; reconstructing the original allocation request is not a guarantee.

Directory readers take bounded copies without opening or waiting on the writer
lock. A receiver verifies that the opened state's UUID and name match the copied
entry and requested name; an identity change returns `SenderNotFound` for retry.
These snapshots are not transactional. Writers still use the library-instance
mutex and interprocess lock. Separate statically linked nozzle copies do not
share the C++ mutex; POSIX process-owned file locks alone do not synchronize
their writer threads.
Zero-length or unpublished state is reported as `SenderNotFound` (not ready).
Sender state and directory magic are published after initialization.

The C++ `sender_desc::metadata` is serialized during registration, before sender
magic and the directory entry are published. A newly connected receiver can read
it without a later `set_metadata()` call or first frame. Initial values and later
updates use the same bounded serializer and existing 512-byte buffer; encoding
and overflow behavior are unchanged. The C sender descriptor has no metadata
field, so this does not add a C API or change its ABI.

Sender/receiver creation preserves a clear version-mismatch error when the
stable header prefix is readable, including older layouts smaller than the
current structure. Discovery's list-only API returns no entries for an invalid
directory and skips malformed entries, as does receiver lookup. The directory
layout version is independent of the sender-state version. Historical directory
versions 2 through 6 share the same layout and remain accepted; sender layouts
still require an exact version match. Align core versions across applications.
For an incompatible persistent directory on macOS/Linux, quit all nozzle apps
and reboot. Do not remove an active application's state to bypass a mismatch.

The open/closed lifecycle marker uses existing zeroed padding at offset 208;
sender-state and directory versions both remain 6, with no size or field-offset
change. Older v6 senders initialize the entire mapping to zero and therefore read
as open, but do not publish closure. With an older sender, only the existing PID
check detects destruction when its process exits; same-process destruction can
still time out. Older receivers likewise ignore the marker, even with a new
sender. Normal frame sharing remains compatible, but both peers must be updated
for the new same-process closure behavior.

Destroying an updated sender publishes closure before
resource cleanup, even while its process stays alive. A receiver that observes
closure during acquisition returns `SenderClosed`, releases its old mapping, and
reports disconnected. Call acquisition again to resolve the original name:
`SenderNotFound` means no open replacement yet, including a closed sender still
visible during teardown. Initial `receiver::create` also returns `SenderNotFound`
for that state; `SenderClosed` describes the end of an existing connection.
A same-name replacement uses its new
UUID, metadata, and frame sequence. This is a retryable lifecycle event, not a
reason for an interactive receiver to permanently stop polling. Process-death
detection remains a fallback; it does not solve PID reuse. Backend waits retain
their existing timeouts and are not asynchronously cancelled by closure.
Connection snapshots and `is_connected()` describe the last observed state, not
an independent liveness probe. Frames already returned retain their ownership.

These checks are compatibility/corruption defenses, not authentication or a
guarantee against malicious concurrent mutation/truncation. Crash cleanup,
PID-reuse-safe liveness, and publication timestamp semantics are separate work:
[nozzle-dev#253](https://github.com/nozzle-io/nozzle-dev/issues/253),
[nozzle-dev#254](https://github.com/nozzle-io/nozzle-dev/issues/254), and
[nozzle-dev#255](https://github.com/nozzle-io/nozzle-dev/issues/255).

### Backend: macOS Metal

Textures are backed by `IOSurface` for cross-process sharing. Sender creates IOSurface-backed Metal textures in a ring buffer. Receiver looks up the IOSurface by ID and creates its own Metal texture view.

ObjC++ code is isolated in `.mm` files. All public headers are pure C++. ARC and non-ARC compatible.

### Backend: Windows D3D11

Textures are created with `D3D11_RESOURCE_MISC_SHARED` for cross-process sharing via `HANDLE`. Sender creates shared textures in a ring buffer. Receiver opens the shared handle and creates its own texture view. Keyed mutex synchronizes access.

Windows registry shared-memory mappings retain one owned handle per normalized
name in each loaded nozzle library instance. Closing a caller's mapping handle
does not remove that retained handle; internal `shm_unlink` releases it, and
library teardown releases any remaining retained handles. Unlink is not POSIX
name removal: existing handles or mapped views, including those in other
processes or independent library instances, can keep the mapping alive.

An abandoned directory mutex is acquired and released normally. Recovery of
mutex ownership does not make interrupted shared-memory writes transactional
or validate directory/state layouts; layout validation is tracked in
[nozzle-dev#189](https://github.com/nozzle-io/nozzle-dev/issues/189).

### Backend: Linux DMA-BUF peer policy

The FD broker admits only peers whose connection-time effective UID matches
the sender's effective UID captured at initialization. Receivers also check the
broker's effective UID before requesting an FD. Credential-query failures are
rejected, with no allow-anyone fallback or implicit root bypass. These are
same-user checks, not authentication of a particular sender or its UUID.

**Compatibility change (nozzle-dev#206):** sharing between different Linux users
is rejected, including a service account and a desktop session. There is no
opt-out. Different login sessions under the same user remain supported.

The supported boundary is ordinary unprivileged desktop processes in the same
user namespace. Cross-user-namespace isolation, root/setuid/capability-bearing
hosts, and credential changes during a sender's lifetime are not supported
security configurations. Credentials are a connection-time snapshot; an FD
already delivered cannot be revoked and can be forwarded by its recipient.
Cached FDs are not continuously reauthorized.

This policy does not protect against malicious same-user peers or privileged
processes, and does not authenticate or make discovery/shared-memory metadata
private. UUIDs and discovery PIDs are identifiers, not credentials. Shared-memory
permissions, ownership and namespace design are tracked separately in
[nozzle-dev#250](https://github.com/nozzle-io/nozzle-dev/issues/250).

### OpenGL Interop

OpenGL integration uses backend-native GPU interop where the platform exposes a
safe path, and keeps copy paths as explicit fallbacks.

- **macOS**: GL texture → IOSurface via `CGLTexImageIOSurface2D` + FBO blit
- **Windows publish**: first tries `WGL_NV_DX_interop2` with a WGL-compatible
  intermediate D3D11 texture, FBO-blits from GL into that texture, then copies on
  GPU into the keyed shared ring texture; if the current WGL context, driver, or
  registration path does not support it, it logs the reason and falls back to
  `glGetTexImage` → D3D11 staging texture → `CopySubresourceRegion`.
- **Windows receive/copy-to-GL**: remains D3D11 staging readback → `glTexSubImage2D`.

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
sources/                     Source lists read by CMake and non-CMake wrappers
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

## x86_64 ISA baseline (Linux)

Distributed x86_64 Linux binaries target **x86-64-v2**: SSE4.2 and POPCNT, but no AVX, AVX2, FMA or F16C. Set this explicitly in release builds. Do not rely on the toolchain default, which moves with the build image. For example, a Linux binary built on GitHub's `ubuntu-26.04` image with no ISA flags contained AVX2/FMA code (nozzle-dev#196).

```bash
cmake -B build -DNOZZLE_X86_64_BASELINE=v2
```

`NOZZLE_X86_64_BASELINE` accepts `v1`, `v2`, `v3` or `v4`. It is empty by default, which keeps the toolchain default. It applies `-march=x86-64[-vN] -mtune=generic` to everything nozzle's own CMake directory builds, and is rejected on non-Linux or non-x86_64 targets. A parent project that adds nozzle with `add_subdirectory()` must also set the flag for its own code.

To verify a built object, archive or executable:

```bash
scripts/check_x86_64_isa.sh build/libnozzle.a path/to/app
```

The script disassembles the given files with `objdump`. It exits non-zero if it finds AVX/EVEX-class instructions or registers, or the x86-64-v3 integer extensions (BMI1/BMI2/LZCNT/MOVBE). It does not detect every non-v2 extension: AES-NI, PCLMUL, SHA, ADX, RDRAND, XSAVE, SSE4a, AMX and APX are not checked. It does not scan shared libraries loaded at runtime. Note that the half-float F16C path in `format_convert` is selected at compile time, so a v2 build uses the scalar path.

## Source lists

Binding enum tests read `tools/enum_manifest.json` from their embedded core.
Regenerate it after C enum edits with `python3 scripts/generate_enum_manifest.py`.
The parser accepts the header's explicit decimal constants and unsigned shifts;
unfamiliar enum syntax fails rather than being omitted. CI checks the committed
JSON and compiles generated C11 `_Static_assert` checks against `nozzle_c.h`.
This covers C enums only, not macros, pixel byte sizes or host/C++ semantic enums.

`sources/*.txt` is the single list of files to compile. CMake reads it, and wrappers that do not use core's CMake must read it too instead of copying paths.

| List | Contents |
|---|---|
| `common.txt` | Required on every platform, including SIMD files guarded internally by architecture |
| `macos.txt` | Metal backend and vImage paths |
| `windows.txt` | D3D11 backend |
| `linux.txt` | DMA-BUF backend |
| `opengl.txt` | Optional OpenGL interop; needs `NOZZLE_HAS_OPENGL=1` |

Each file has one path per line, relative to the core root, with no comments or blank lines.

Makefiles can include the helper, which stops the build if a list cannot be read:

```make
NOZZLE_DIR := deps/nozzle
include $(NOZZLE_DIR)/sources/sources.mk

ALL_SRCS := $(NOZZLE_COMMON_SRCS) $(NOZZLE_OPENGL_SRCS) $(NOZZLE_LINUX_SRCS)
```

Other build systems should read the text files and fail if a list is missing or empty. An empty list can still link when using `-shared`, which would leave a library with no core code.

These lists cover sources only. Wrappers still set compile definitions and link flags themselves.

To add a source, add it to one list. `scripts/check_source_manifest.sh` fails if a file under `src/` is in no list or in more than one.

Avoid adding a new list. Wrappers choose which lists to compile, so a new list needs a change in every wrapper, and the check cannot tell whether a wrapper uses it.

Packaging allowlists must include `sources/`, for example `include` in `Cargo.toml` and in `[tool.scikit-build.sdist]`.

## Design Decisions

| Decision | Resolution |
|----------|------------|
| Namespace | `nozzle` |
| Naming | All snake_case in library, `Nozzle` prefix for C API |
| Error handling | `Result<T>` — no exceptions across API boundaries |
| Thread safety | Object-level (same sender/receiver callable from multiple threads; calls on one object are serialized, so a blocking `acquire_frame` holds that receiver until it returns) |
| Logging | plog (header-only, submodule) |
| Crash cleanup | Lazy — receiver detects dead sender on access failure |

## Acknowledgments

Nozzle draws significant inspiration from [Syphon](https://syphon.info/) and [Spout](http://spout.zeal.co/). The named sender/receiver model, the concept of shared GPU textures between local processes, and the focus on creative-coding and real-time graphics integration all follow the path they established. While nozzle is an independent implementation with its own API design and is not protocol-compatible with either project, their work directly informed the architecture and goals. Much respect to the Syphon and Spout developers for proving this model works and matters.

## License

MIT

Third-party dependencies:

- [plog](https://github.com/SergiusTheBest/plog) — MIT
