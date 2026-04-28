#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace bbb {
namespace nozzle {

enum class BackendType {
    Unknown,
    D3D11,
    Metal,
    OpenGL,
};

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

enum class TransferMode {
    Unknown,
    ZeroCopySharedTexture,
    GpuCopy,
    CpuCopy,
};

enum class SyncMode {
    None,
    AccessGuarded,
    GpuFenceBestEffort,
};

enum class ReceiveMode {
    LatestOnly,
    SequentialBestEffort,
};

enum class FrameStatus {
    NewFrame,
    NoNewFrame,
    DroppedFrames,
    SenderClosed,
    Error,
};

enum class TextureUsage : uint32_t {
    None = 0,
    ShaderRead = 1 << 0,
    ShaderWrite = 1 << 1,
    RenderTarget = 1 << 2,
    Shared = 1 << 3,
};

constexpr TextureUsage operator|(TextureUsage a, TextureUsage b) {
    return static_cast<TextureUsage>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

constexpr TextureUsage operator&(TextureUsage a, TextureUsage b) {
    return static_cast<TextureUsage>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}

struct FrameInfo {
    uint64_t frameIndex{0};
    uint64_t timestampNs{0};
    uint32_t width{0};
    uint32_t height{0};
    TextureFormat format{TextureFormat::Unknown};
    TransferMode transferMode{TransferMode::Unknown};
    SyncMode syncMode{SyncMode::None};
    uint32_t droppedFrameCount{0};
};

struct TextureDesc {
    uint32_t width{0};
    uint32_t height{0};
    TextureFormat format{TextureFormat::Unknown};
    TextureUsage usage{TextureUsage::ShaderRead | TextureUsage::Shared};
};

struct TextureLayout {
    uint32_t rowPitchBytes{0};
    uint32_t slicePitchBytes{0};
    uint32_t alignmentBytes{0};
};

struct TextureFormatDesc {
    TextureFormat commonFormat{TextureFormat::Unknown};
    uint32_t nativeFormat{0};
};

struct SenderInfo {
    std::string name{};
    std::string applicationName{};
    std::string id{};
    BackendType backend{BackendType::Unknown};
};

struct ConnectedSenderInfo {
    std::string name{};
    std::string applicationName{};
    std::string id{};
    BackendType backend{BackendType::Unknown};
    uint32_t width{0};
    uint32_t height{0};
    TextureFormat format{TextureFormat::Unknown};
    double estimatedFps{0.0};
    uint64_t frameCounter{0};
    uint64_t lastUpdateTimeNs{0};
};

struct KeyValue {
    std::string key{};
    std::string value{};
};

using Metadata = std::vector<KeyValue>;

struct SenderDesc {
    std::string name{};
    std::string applicationName{};
    uint32_t ringBufferSize{3};
    Metadata metadata{};
};

struct ReceiverDesc {
    std::string name{};
    std::string applicationName{};
    ReceiveMode receiveMode{ReceiveMode::LatestOnly};
};

struct AcquireDesc {
    uint64_t timeoutMs{0};
};

} // namespace nozzle
} // namespace bbb
