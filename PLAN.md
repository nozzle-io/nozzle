# Nozzle PLAN.md

## 0. Project Summary

**Nozzle** is a C/C++ texture sharing library for local inter-process GPU texture sharing on macOS and Windows.

It follows the spirit of Syphon and Spout, but intentionally does not aim to be protocol-compatible with either in v0.1. The main design goal is to provide a shared high-level sender/receiver API while still exposing backend-native GPU handles where necessary.

Nozzle is intended to support high-precision and non-8bit texture formats such as `R32F`, `RGBA16F`, and `RGBA32F`, and to be easy to embed as a normal static library or source-integrated dependency.

Primary early targets:

- openFrameworks addon
- Max external
- custom C++ realtime graphics applications

---

## 1. Core Goals

### 1.1 Product Goals

Nozzle should provide:

1. Named GPU texture sharing between local applications.
2. A mostly identical C++ API on macOS and Windows.
3. A C ABI for plugin and host integration.
4. Multiple receivers reading from the same sender.
5. Sender discovery for user-facing selection UIs.
6. Support for high-precision texture formats beyond classic 8bit RGBA.
7. Low-level backend access for performance-critical integrations.
8. Simple static-library or source-level integration.
9. Minimal or zero external runtime dependencies.
10. A design that can later support Syphon/Spout bridge modules without coupling the core to those protocols.

### 1.2 Non-Goals for v0.1

Nozzle v0.1 does **not** aim to provide:

- Syphon protocol compatibility.
- Spout protocol compatibility.
- Network texture sharing.
- Reliable unlimited frame queues.
- Vulkan backend.
- D3D12 backend.
- CUDA interop.
- Full OpenGL backend parity.
- Full zero-copy guarantees across all APIs.
- Support guarantees for every GPU texture format.
- JSON dependency for metadata.
- Dynamic-library-first plugin system.

---

## 2. v0.1 Scope

### 2.1 Definition

Nozzle v0.1 is a C/C++17 static library for named GPU texture sharing between local processes on Windows and macOS.

The core backends are:

- **Windows:** D3D11 shared textures.
- **macOS:** Metal textures backed by IOSurface.

The API exposes both:

- A common sender/receiver abstraction.
- Backend-native handles for low-level integration.

v0.1 prioritizes:

- Multiple receivers per sender.
- Sender discovery.
- High-precision texture formats.
- RAII frame lifetime.
- Best-effort sequential frame access.
- Latest-frame realtime access.
- Minimal dependencies.
- openFrameworks and Max external integration.

---

## 3. Architecture Overview

Nozzle should be structured in four layers.

```text
Layer 4: Integration wrappers
         openFrameworks addon, Max external, future Unity/Unreal/etc.

Layer 3: Optional helper modules
         OpenGL interop helpers, convenience copy utilities

Layer 2: Backend-native API
         D3D11, Metal/IOSurface, native texture/device handles

Layer 1: Common API
         Sender, Receiver, Frame, Texture, Device, Discovery, Metadata

Layer 0: Platform/backend infrastructure
         Registry, IPC, shared memory, synchronization, format mapping
```

The common API should be usable from identical user code where the user does not need to touch backend-native handles.

However, Nozzle should not pretend that all GPU APIs can be fully abstracted without tradeoffs. Backend-native escape hatches are part of the core design.

---

## 4. Backend Strategy

## 4.1 Windows v0.1 Backend

Primary backend:

```text
D3D11 shared texture
```

Required native objects:

```cpp
ID3D11Device*
ID3D11DeviceContext*
ID3D11Texture2D*
HANDLE sharedHandle
DXGI_FORMAT
```

Expected implementation concepts:

- Create or accept D3D11 textures with shared-handle capability.
- Share texture handles through a local registry/IPC mechanism.
- Allow receivers to open shared textures using their own D3D11 device.
- Use D3D11 synchronization primitives where appropriate.
- Support keyed mutex or equivalent access guarding when available/needed.

The Windows backend should be the canonical implementation for applications that already operate in Direct3D 11.

### 4.2 macOS v0.1 Backend

Primary backend:

```text
Metal texture backed by IOSurface
```

Required native objects:

```objc
id<MTLDevice>
id<MTLTexture>
IOSurfaceRef
MTLPixelFormat
```

Expected implementation concepts:

- Create or accept IOSurface-backed Metal textures.
- Share IOSurface identity through local IPC/registry data.
- Allow receivers to wrap the IOSurface as a Metal texture on their own MTLDevice.
- Use Metal command buffer completion and IOSurface visibility rules internally.

The macOS backend should be the canonical implementation for modern macOS graphics applications.

### 4.3 OpenGL Support

OpenGL is desired for openFrameworks and Max/Jitter workflows, but it should not be treated as the primary core backend in v0.1.

v0.1 should instead provide OpenGL helper modules:

```cpp
namespace nozzle::gl {
    Result<Texture> wrapGLTexture(GLuint texture, const GLTextureDesc& desc);
    Result<void> publishGLTexture(Sender& sender, GLuint texture, const GLTextureDesc& desc);
    Result<void> copyFrameToGLTexture(const Frame& frame, GLuint dstTexture, const GLTextureDesc& dstDesc);
}
```

Initial OpenGL support may involve internal GPU copies:

- Windows: OpenGL texture copied to/from D3D11 shared texture via interop or staging path.
- macOS: OpenGL texture copied to/from Metal/IOSurface path where available.

OpenGL helpers should be marked experimental until their performance and synchronization behavior are well understood.

---

## 5. API Model

### 5.1 Named Sender/Receiver Model

Nozzle v0.1 uses a named sender/receiver model.

Example:

```cpp
nozzle::SenderDesc desc;
desc.name = "Main";
desc.applicationName = "MyApp";

auto senderResult = nozzle::Sender::create(desc);
if (!senderResult) {
    // handle error
}

nozzle::ReceiverDesc receiverDesc;
receiverDesc.name = "Main";
receiverDesc.applicationName = "MyApp";

auto receiverResult = nozzle::Receiver::create(receiverDesc);
```

Discovery is available, but the main connection model remains name-oriented.

### 5.2 Discovery

Discovery is required in v0.1.

```cpp
std::vector<nozzle::SenderInfo> nozzle::enumerateSenders();
```

v0.1 required fields:

```cpp
struct SenderInfo {
    std::string name;
    std::string applicationName;
    std::string id;
    BackendType backend;
};
```

Additional information should be available after connecting to a sender:

```cpp
struct ConnectedSenderInfo {
    std::string name;
    std::string applicationName;
    std::string id;
    BackendType backend;

    uint32_t width;
    uint32_t height;
    TextureFormat format;
    double estimatedFps;
    uint64_t frameCounter;
    uint64_t lastUpdateTimeNs;
    Metadata metadata;
};
```

### 5.3 Sender Name Collision Policy

Primary identity:

```text
name + applicationName
```

If a collision still occurs, Nozzle should auto-rename using a short generated suffix:

```text
Main
Main-a3f91c2b
```

The suffix should be derived from an internal unique id, approximately 8 characters long.

---

## 6. Texture Ownership Model

Nozzle should support both user-owned and Nozzle-owned texture paths.

### 6.1 User-Owned Texture Publishing

The user provides a backend-native texture or wrapped Nozzle texture.

```cpp
auto texture = nozzle::d3d11::wrapTexture(d3dTexture, textureDesc);
sender.publishExternalTexture(texture);
```

This is useful when the host framework already owns textures, such as openFrameworks, Max/Jitter, or a custom renderer.

### 6.2 Nozzle-Owned Shared Texture

Nozzle creates a shareable texture and exposes it for rendering/copying.

```cpp
auto frameResult = sender.acquireWritableFrame(frameDesc);
if (frameResult) {
    auto& frame = *frameResult;
    renderInto(frame.texture());
    sender.commitFrame(frame);
}
```

This path is useful for performance-critical integrations where avoiding unnecessary copies matters.

### 6.3 Transfer Mode Reporting

The API should expose whether a given operation was zero-copy or copy-based.

```cpp
enum class TransferMode {
    Unknown,
    ZeroCopySharedTexture,
    GpuCopy,
    CpuCopy
};
```

Frame info should include:

```cpp
TransferMode transferMode;
```

---

## 7. Frame Model

### 7.1 RAII Frame Lifetime

Receiver-acquired frames should use RAII.

```cpp
auto frameResult = receiver.acquireFrame();
if (frameResult) {
    nozzle::Frame frame = std::move(*frameResult);
    auto texture = frame.texture();
}
```

The backend texture handle is valid while the `Frame` object is alive.

Explicit release should also be available:

```cpp
frame.release();
```

### 7.2 Frame Clone

Frames should support explicit cloning into an owned texture.

```cpp
auto ownedTexture = frame.cloneToOwnedTexture(device);
```

This is useful when:

- A receiver needs to retain a frame beyond the ring buffer lifetime.
- A framework requires ownership transfer.
- A consumer wants to avoid holding shared resources.

### 7.3 Ring Buffer

Senders should use a small ring buffer.

Default:

```cpp
uint32_t ringBufferSize = 3;
```

The ring buffer allows:

- Multiple receivers.
- Temporary receiver lag.
- Best-effort sequential reads.
- Avoiding immediate destruction of recently published textures.

If receivers hold old frames for too long, Nozzle may drop frames or overwrite older ring entries depending on mode.

---

## 8. Receive Modes

v0.1 should support two receive modes.

```cpp
enum class ReceiveMode {
    LatestOnly,
    SequentialBestEffort
};
```

### 8.1 LatestOnly

Realtime mode.

Behavior:

- Receiver always acquires the newest available frame.
- Dropped intermediate frames are acceptable.
- Best default for VJ, realtime graphics, and preview workflows.

### 8.2 SequentialBestEffort

Best-effort ordered mode.

Behavior:

- Receiver attempts to read frames in order from the ring buffer.
- If the receiver falls behind and the ring buffer overflows, Nozzle reports dropped frames.
- This mode does not guarantee reliable delivery of every frame.

Frame status:

```cpp
enum class FrameStatus {
    NewFrame,
    NoNewFrame,
    DroppedFrames,
    SenderClosed,
    Error
};
```

---

## 9. Synchronization

Nozzle should explicitly model synchronization instead of hiding it completely.

```cpp
enum class SyncMode {
    None,
    AccessGuarded,
    GpuFenceBestEffort
};
```

v0.1 should implement at least:

```text
AccessGuarded + latest/sequential frame bookkeeping
```

Backend-specific notes:

- Windows D3D11 may use keyed mutex or related access protection where available.
- macOS may use Metal command buffer completion and IOSurface-backed resource visibility boundaries.

Frame info should expose the synchronization mode used:

```cpp
struct FrameInfo {
    uint64_t frameIndex;
    uint64_t timestampNs;
    uint32_t width;
    uint32_t height;
    TextureFormat format;
    TransferMode transferMode;
    SyncMode syncMode;
    uint32_t droppedFrameCount;
};
```

v0.1 does not promise full producer/consumer frame synchronization. The design remains realtime-first.

---

## 10. Texture Format Strategy

### 10.1 Common Format Enum

Nozzle should include a broad common format enum from v0.1, even if not all values are fully tested initially.

```cpp
enum class TextureFormat {
    Unknown,

    R8_UNorm,
    RG8_UNorm,
    RGBA8_UNorm,
    BGRA8_UNorm,
    RGBA8_SRGB,
    BGRA8_SRGB,

    R16_UNorm,
    RG16_UNorm,
    RGBA16_UNorm,

    R16_Float,
    RG16_Float,
    RGBA16_Float,

    R32_Float,
    RG32_Float,
    RGBA32_Float,

    R32_UInt,
    RGBA32_UInt,

    Depth32_Float,
};
```

### 10.2 v0.1 Must-Pass Formats

The following formats should be part of the v0.1 test matrix:

```text
RGBA8_UNorm
BGRA8_UNorm
R32_Float
RGBA16_Float
RGBA32_Float
```

These formats demonstrate the core Nozzle value proposition: not being limited to 8bit RGBA.

### 10.3 Native Format Escape Hatch

To support the long-term goal of “any texture format that the machine can handle,” Nozzle needs a native format escape hatch.

```cpp
struct TextureFormatDesc {
    TextureFormat commonFormat;
    uint32_t nativeFormat;
};
```

`nativeFormat` maps to:

- `DXGI_FORMAT` on Windows.
- `MTLPixelFormat` on macOS.

Nozzle should provide format queries:

```cpp
bool device.supportsFormat(TextureFormat format, TextureUsage usage);
bool device.supportsNativeFormat(uint32_t nativeFormat, TextureUsage usage);
```

### 10.4 Row Pitch / Alignment

High-level API should hide row pitch and alignment where possible.

Low-level API should expose it when needed:

```cpp
struct TextureLayout {
    uint32_t rowPitchBytes;
    uint32_t slicePitchBytes;
    uint32_t alignmentBytes;
};
```

---

## 11. Metadata

v0.1 should support minimal metadata without adding a JSON dependency.

```cpp
struct KeyValue {
    std::string key;
    std::string value;
};

using Metadata = std::vector<KeyValue>;
```

Required frame metadata fields should be represented as typed fields rather than string metadata:

```cpp
uint64_t frameIndex;
uint64_t timestampNs;
```

Recommended optional metadata keys:

```text
value_range
color_space
channel_semantics
origin
units
```

Examples:

```text
value_range = "0:1"
value_range = "unbounded"
color_space = "linear"
color_space = "srgb"
channel_semantics = "rgba"
channel_semantics = "depth"
origin = "top-left"
origin = "bottom-left"
```

---

## 12. Error Handling

Nozzle C++ API should not use exceptions.

Recommended pattern:

```cpp
template <typename T>
class Result;

struct Error {
    ErrorCode code;
    std::string message;
};
```

Example:

```cpp
auto sender = nozzle::Sender::create(desc);
if (!sender) {
    log(sender.error().message);
    return;
}
```

Error codes:

```cpp
enum class ErrorCode {
    Ok,
    Unknown,
    InvalidArgument,
    UnsupportedBackend,
    UnsupportedFormat,
    DeviceMismatch,
    ResourceCreationFailed,
    SharedHandleFailed,
    SenderNotFound,
    SenderClosed,
    Timeout,
    BackendError,
};
```

---

## 13. C ABI

C API should be included from the beginning.

### 13.1 Opaque Handles

```c
typedef struct NozzleSender NozzleSender;
typedef struct NozzleReceiver NozzleReceiver;
typedef struct NozzleFrame NozzleFrame;
typedef struct NozzleTexture NozzleTexture;
typedef struct NozzleDevice NozzleDevice;
```

### 13.2 Result Type

```c
typedef enum NozzleErrorCode {
    NOZZLE_OK = 0,
    NOZZLE_ERROR_UNKNOWN,
    NOZZLE_ERROR_INVALID_ARGUMENT,
    NOZZLE_ERROR_UNSUPPORTED_BACKEND,
    NOZZLE_ERROR_UNSUPPORTED_FORMAT,
    NOZZLE_ERROR_DEVICE_MISMATCH,
    NOZZLE_ERROR_RESOURCE_CREATION_FAILED,
    NOZZLE_ERROR_SHARED_HANDLE_FAILED,
    NOZZLE_ERROR_SENDER_NOT_FOUND,
    NOZZLE_ERROR_SENDER_CLOSED,
    NOZZLE_ERROR_TIMEOUT,
    NOZZLE_ERROR_BACKEND_ERROR,
} NozzleErrorCode;
```

### 13.3 Example C API

```c
NozzleErrorCode nozzle_sender_create(
    const NozzleSenderDesc* desc,
    NozzleSender** out_sender
);

void nozzle_sender_destroy(NozzleSender* sender);

NozzleErrorCode nozzle_receiver_create(
    const NozzleReceiverDesc* desc,
    NozzleReceiver** out_receiver
);

void nozzle_receiver_destroy(NozzleReceiver* receiver);

NozzleErrorCode nozzle_receiver_acquire_frame(
    NozzleReceiver* receiver,
    const NozzleAcquireDesc* desc,
    NozzleFrame** out_frame
);

void nozzle_frame_release(NozzleFrame* frame);
```

Backend-native wrappers should also be exposed carefully through C for plugin integrations.

---

## 14. Build and Distribution

### 14.1 v0.1 Build Goals

Nozzle should be easy to embed.

Preferred distribution:

```text
- Static library
- CMake project
- Source-integration friendly layout
- No external runtime dependency
```

Header-only would be ideal but is not required. Because Nozzle needs Objective-C++, COM, OS handles, IPC, and backend implementations, pure header-only is likely not worth forcing.

### 14.2 Dependencies

v0.1 should target dependency-zero for core runtime code.

Allowed:

- C++17 standard library.
- OS frameworks and system SDKs.
- Objective-C++ internally on macOS.
- COM/D3D/DXGI internally on Windows.
- Extremely permissive single-header utilities only if clearly justified.

Avoid:

- Boost as a required dependency.
- fmt as a required dependency.
- JSON library as a required dependency.
- Dynamic runtime dependencies.

### 14.3 Suggested Repository Layout

```text
nozzle/
  CMakeLists.txt
  LICENSE
  README.md
  PLAN.md

  include/
    nozzle/nozzle.hpp
    nozzle/nozzle_c.h
    nozzle/result.hpp
    nozzle/types.hpp
    nozzle/sender.hpp
    nozzle/receiver.hpp
    nozzle/frame.hpp
    nozzle/texture.hpp
    nozzle/device.hpp

    nozzle/backends/d3d11.hpp
    nozzle/backends/metal.hpp
    nozzle/helpers/opengl.hpp

  src/
    common/
      registry.cpp
      sender.cpp
      receiver.cpp
      frame.cpp
      texture.cpp
      metadata.cpp

    c_api/
      nozzle_c.cpp

    backends/
      d3d11/
        d3d11_backend.cpp
        d3d11_texture.cpp
        d3d11_sync.cpp

      metal/
        metal_backend.mm
        metal_texture.mm
        metal_sync.mm

    helpers/
      opengl/
        opengl_helpers.cpp

  examples/
    simple_sender/
    simple_receiver/
    d3d11_sender/
    metal_sender/
    gl_sender_experimental/

  tests/
    format_tests/
    discovery_tests/
    ring_buffer_tests/
    multi_receiver_tests/
```

---

## 15. Platform Macros

Nozzle should provide stable feature macros.

```cpp
#define NOZZLE_PLATFORM_WINDOWS
#define NOZZLE_PLATFORM_MACOS

#define NOZZLE_HAS_D3D11
#define NOZZLE_HAS_METAL
#define NOZZLE_HAS_OPENGL_HELPERS
```

Backend-specific code should be explicitly guarded:

```cpp
#if NOZZLE_HAS_D3D11
#include <nozzle/backends/d3d11.hpp>
#endif

#if NOZZLE_HAS_METAL
#include <nozzle/backends/metal.hpp>
#endif
```

The goal is not to hide platform differences completely. The goal is to make the common path identical and the platform-specific path explicit and controlled.

---

## 16. High-Level API Sketch

```cpp
namespace nozzle {

struct SenderDesc {
    std::string name;
    std::string applicationName;
    uint32_t ringBufferSize = 3;
    Metadata metadata;
};

struct ReceiverDesc {
    std::string name;
    std::string applicationName;
    ReceiveMode receiveMode = ReceiveMode::LatestOnly;
};

class Sender {
public:
    static Result<Sender> create(const SenderDesc& desc);

    Result<void> publishExternalTexture(const Texture& texture);
    Result<WritableFrame> acquireWritableFrame(const TextureDesc& desc);
    Result<void> commitFrame(WritableFrame& frame);

    SenderInfo info() const;
    Result<void> setMetadata(const Metadata& metadata);
};

class Receiver {
public:
    static Result<Receiver> create(const ReceiverDesc& desc);

    Result<Frame> acquireFrame();
    Result<Frame> acquireFrame(const AcquireDesc& desc);

    ConnectedSenderInfo connectedInfo() const;
};

class Frame {
public:
    FrameInfo info() const;
    const Texture& texture() const;

    Result<Texture> cloneToOwnedTexture(Device& device) const;
    void release();
};

std::vector<SenderInfo> enumerateSenders();

} // namespace nozzle
```

---

## 17. Backend-Native API Sketch

### 17.1 D3D11

```cpp
namespace nozzle::d3d11 {

struct DeviceDesc {
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
};

Result<Device> wrapDevice(const DeviceDesc& desc);

struct TextureWrapDesc {
    ID3D11Texture2D* texture = nullptr;
    DXGI_FORMAT format;
    uint32_t width;
    uint32_t height;
};

Result<Texture> wrapTexture(const TextureWrapDesc& desc);

ID3D11Texture2D* getTexture(const Texture& texture);
HANDLE getSharedHandle(const Texture& texture);

} // namespace nozzle::d3d11
```

### 17.2 Metal / IOSurface

```objc
namespace nozzle::metal {

struct DeviceDesc {
    id<MTLDevice> device = nil;
};

Result<Device> wrapDevice(const DeviceDesc& desc);

struct TextureWrapDesc {
    id<MTLTexture> texture = nil;
    IOSurfaceRef ioSurface = nullptr;
    MTLPixelFormat format;
    uint32_t width;
    uint32_t height;
};

Result<Texture> wrapTexture(const TextureWrapDesc& desc);

id<MTLTexture> getTexture(const Texture& texture);
IOSurfaceRef getIOSurface(const Texture& texture);

} // namespace nozzle::metal
```

---

## 18. Testing Strategy

### 18.1 Required v0.1 Tests

Common tests:

- Sender creation.
- Receiver creation.
- Sender discovery.
- Name collision handling.
- Multiple receiver access.
- LatestOnly receive mode.
- SequentialBestEffort receive mode.
- Ring buffer overflow and dropped-frame reporting.
- RAII frame release.
- Frame clone.
- Metadata set/get.
- C API smoke tests.

Format tests:

- `RGBA8_UNorm`
- `BGRA8_UNorm`
- `R32_Float`
- `RGBA16_Float`
- `RGBA32_Float`

Backend tests:

- D3D11 sender to D3D11 receiver across processes.
- Metal sender to Metal receiver across processes.
- OpenGL helper publish/acquire path where available.

### 18.2 v0.1 Success Criteria

Windows:

- D3D11 texture can be published from one process.
- Another process can receive and read the shared texture.
- Multiple receivers can read the same sender.
- Must-pass formats work.
- openFrameworks addon can send/receive GL textures, even if internally copy-based.

macOS:

- Metal/IOSurface-backed texture can be published from one process.
- Another process can receive and read the shared texture.
- Multiple receivers can read the same sender.
- Must-pass formats work.
- openFrameworks addon can send/receive GL textures, even if internally copy-based.

Common:

- Same high-level C++ code can compile for sender/receiver workflows on both platforms.
- Discovery returns sender name, application name, id, and backend.
- Collision handling works.
- Frame metadata and frame index are available.
- Static library integration works.
- No external runtime dependencies are required.

---

## 19. Phases

### Phase 0: Design Spike

Deliverables:

- Finalize public type names.
- Finalize error model.
- Finalize format enum and backend format mapping.
- Prototype registry/IPC mechanism on each OS.
- Confirm feasibility of D3D11 shared handle flow.
- Confirm feasibility of Metal + IOSurface flow.

### Phase 1: Core v0.1

Deliverables:

- Common C++ API.
- D3D11 backend.
- Metal/IOSurface backend.
- Discovery.
- Ring buffer.
- LatestOnly and SequentialBestEffort modes.
- Metadata.
- C API.
- Static library CMake build.
- Basic examples.

### Phase 2: Integration v0.1

Deliverables:

- openFrameworks addon.
- Max external.
- Experimental OpenGL helpers.
- Cross-process sample apps.
- Format test apps.

### Phase 3: Bridge and Extended Backends

Deliverables:

- Optional Spout bridge.
- Optional Syphon bridge.
- D3D12 investigation.
- Vulkan investigation.
- Expanded format coverage.
- Better GPU fence model.

---

## 20. Design Principles

1. **Realtime-first, not reliability-first.**
   Dropping frames is acceptable in default mode.

2. **High precision is a core feature.**
   Nozzle must not be architecturally limited to 8bit RGBA.

3. **Expose native handles.**
   Serious graphics integrations need backend access.

4. **Make copies visible.**
   If Nozzle performs a GPU or CPU copy, the user should be able to inspect that fact.

5. **Avoid unnecessary dependencies.**
   Embedding Nozzle should be easier than embedding Syphon/Spout-style projects.

6. **Keep compatibility separate.**
   Syphon/Spout compatibility belongs in bridge modules, not the core.

7. **Prefer simple ownership rules.**
   Use RAII in C++; use explicit retain/release style in C.

8. **Do not over-abstract GPU differences.**
   Common code should be common, but backend-specific realities should remain accessible.

---

## 21. Open Questions

These should be resolved during Phase 0:

1. Exact IPC/registry mechanism per OS.
2. Whether sender registry should be process-local service, shared memory, named mutex/file mapping, or OS-specific notification system.
3. Exact D3D11 synchronization primitive strategy.
4. Exact Metal/IOSurface synchronization and visibility strategy.
5. Whether `applicationName` should be user-provided only or auto-detected with override.
6. Whether `timestampNs` should use monotonic time, host time, or backend GPU timestamp.
7. How strict format compatibility should be when receiver device differs from sender device.
8. Whether OpenGL helpers live in core repository or separate package.
9. Minimum supported macOS version.
10. Minimum supported Windows version.

---

## 22. Initial Recommended v0.1 Statement

Nozzle v0.1 will provide a dependency-light C/C++17 static library for local named GPU texture sharing between applications on Windows and macOS.

It will use D3D11 shared textures on Windows and Metal textures backed by IOSurface on macOS. The API will provide a common sender/receiver model, discovery, multiple receiver support, high-precision texture formats, RAII frame lifetime, best-effort frame sequencing, and backend-native escape hatches.

OpenGL support will be provided as experimental helper functionality, primarily to support openFrameworks and Max integration. Syphon/Spout protocol compatibility will be deferred to optional bridge modules in a later phase.
