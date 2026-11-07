#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11_1.h>
#include <dxgi.h>

#include <nozzle/nozzle.hpp>
#include <nozzle/backends/d3d11.hpp>
#include <nozzle/pixel_access.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>
#include <string>
#include <utility>

namespace {

using steady_clock = std::chrono::steady_clock;
constexpr uint32_t width = 4;
constexpr uint32_t height = 3;

double elapsed_ms(steady_clock::time_point start) {
    return std::chrono::duration<double, std::milli>(steady_clock::now() - start).count();
}

template <typename T> struct com_owner {
    T *ptr{nullptr};
    com_owner() = default;
    com_owner(const com_owner &) = delete;
    com_owner &operator=(const com_owner &) = delete;
    ~com_owner() { if (ptr) ptr->Release(); }
};

nozzle::texture_desc texture_desc() {
    nozzle::texture_desc desc{};
    desc.width = width;
    desc.height = height;
    desc.format = nozzle::texture_format::rgba8_unorm;
    return desc;
}

nozzle::sender make_sender(uint32_t ring_size = 1) {
    nozzle::sender_desc desc{};
    desc.name = "writable_lease_" + std::to_string(GetCurrentProcessId()) + "_" +
        std::to_string(steady_clock::now().time_since_epoch().count());
    desc.ring_buffer_size = ring_size;
    auto result = nozzle::sender::create(desc);
    INFO(result.error().message);
    REQUIRE(result.ok());
    REQUIRE(result.value().info().backend == nozzle::backend_type::d3d11);
    return std::move(result.value());
}

uint8_t pattern(uint32_t seed, uint32_t x, uint32_t y, uint32_t channel) {
    return static_cast<uint8_t>((seed * 29 + x * 37 + y * 61 + channel * 47 + 13) & 255);
}

bool transfer_pattern(const nozzle::mapped_pixels &pixels, uint32_t seed, bool write) {
    if (!pixels.data || pixels.width != width || pixels.height != height) return false;
    const bool bgra = pixels.format == nozzle::texture_format::bgra8_unorm;
    if (!bgra && pixels.format != nozzle::texture_format::rgba8_unorm) return false;
    for (uint32_t y = 0; y < height; ++y) {
        auto *row = static_cast<uint8_t *>(pixels.data) + y * pixels.row_stride_bytes;
        for (uint32_t x = 0; x < width; ++x) {
            for (uint32_t c = 0; c < 4; ++c) {
                const auto semantic = bgra && (c == 0 || c == 2) ? 2 - c : c;
                const auto value = pattern(seed, x, y, semantic);
                if (write) row[x * 4 + c] = value;
                else if (row[x * 4 + c] != value) return false;
            }
        }
    }
    return true;
}

void write_pixels(nozzle::writable_frame &frame, uint32_t seed) {
    auto mapping = nozzle::lock_writable_pixels_mapping_with_origin(frame, nozzle::texture_origin::top_left);
    INFO(mapping.error().message);
    REQUIRE(mapping.ok());
    REQUIRE(transfer_pattern(mapping.value().pixels(), seed, true));
    REQUIRE(mapping.value().unlock_checked().ok());
}

bool matches_pixels(const nozzle::frame &frame, uint32_t seed) {
    auto mapping = nozzle::lock_frame_pixels_mapping_with_origin(frame, nozzle::texture_origin::top_left);
    if (!mapping.ok()) return false;
    const bool matched = transfer_pattern(mapping.value().pixels(), seed, false);
    const auto unlocked = mapping.value().unlock_checked();
    return matched && unlocked.ok();
}

void publish(nozzle::sender &sender, uint32_t seed) {
    auto writable = sender.acquire_writable_frame(texture_desc());
    INFO(writable.error().message);
    REQUIRE(writable.ok());
    write_pixels(writable.value(), seed);
    REQUIRE(sender.commit_frame(writable.value()).ok());
}

// Open before owner destruction. COM references keep the import alive, but do
// not acquire the mutex. A distinct device prevents recursive-acquire artifacts.
struct imported_probe {
    com_owner<ID3D11Device> device;
    com_owner<ID3D11DeviceContext> context;
    com_owner<ID3D11Texture2D> texture;
    com_owner<IDXGIKeyedMutex> mutex;
    bool owned{false};

    ~imported_probe() { if (owned) mutex.ptr->ReleaseSync(0); }

    void open(const nozzle::texture &source) {
        auto *native = nozzle::d3d11::get_texture(source);
        REQUIRE(native != nullptr);
        com_owner<ID3D11Device> source_device;
        native->GetDevice(&source_device.ptr);
        REQUIRE(source_device.ptr != nullptr);
        com_owner<IDXGIDevice> dxgi_device;
        REQUIRE(source_device.ptr->QueryInterface(__uuidof(IDXGIDevice),
            reinterpret_cast<void **>(&dxgi_device.ptr)) == S_OK);
        com_owner<IDXGIAdapter> adapter;
        REQUIRE(dxgi_device.ptr->GetAdapter(&adapter.ptr) == S_OK);
        D3D_FEATURE_LEVEL level{};
        REQUIRE(D3D11CreateDevice(adapter.ptr, D3D_DRIVER_TYPE_UNKNOWN, nullptr,
            D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION,
            &device.ptr, &level, &context.ptr) == S_OK);
        REQUIRE(device.ptr != source_device.ptr);
        com_owner<ID3D11Device1> device1;
        REQUIRE(device.ptr->QueryInterface(__uuidof(ID3D11Device1),
            reinterpret_cast<void **>(&device1.ptr)) == S_OK);
        const HANDLE handle = nozzle::d3d11::get_shared_handle(source);
        REQUIRE(handle != nullptr);
        REQUIRE(device1.ptr->OpenSharedResource1(handle, __uuidof(ID3D11Texture2D),
            reinterpret_cast<void **>(&texture.ptr)) == S_OK);
        REQUIRE(texture.ptr->QueryInterface(__uuidof(IDXGIKeyedMutex),
            reinterpret_cast<void **>(&mutex.ptr)) == S_OK);
    }

    HRESULT acquire() {
        REQUIRE_FALSE(owned);
        const HRESULT hr = mutex.ptr->AcquireSync(0, 0);
        owned = hr == S_OK;
        return hr;
    }

    void release_checked() {
        if (!owned) return;
        const HRESULT hr = mutex.ptr->ReleaseSync(0);
        if (hr == S_OK) owned = false;
        REQUIRE(hr == S_OK);
    }

    void check_held() {
        const HRESULT hr = acquire();
        // Release an unexpectedly available lock before any fatal assertion or
        // subsequent producer operation; never leave a successful probe held.
        release_checked();
        CHECK(hr == static_cast<HRESULT>(WAIT_TIMEOUT));
    }

    bool read_pattern(uint32_t seed) {
        REQUIRE(owned);
        D3D11_TEXTURE2D_DESC desc{};
        texture.ptr->GetDesc(&desc);
        const auto format = desc.Format == DXGI_FORMAT_B8G8R8A8_UNORM
            ? nozzle::texture_format::bgra8_unorm : nozzle::texture_format::rgba8_unorm;
        REQUIRE((desc.Format == DXGI_FORMAT_B8G8R8A8_UNORM || desc.Format == DXGI_FORMAT_R8G8B8A8_UNORM));
        desc.Usage = D3D11_USAGE_STAGING;
        desc.BindFlags = 0;
        desc.MiscFlags = 0;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        com_owner<ID3D11Texture2D> staging;
        REQUIRE(device.ptr->CreateTexture2D(&desc, nullptr, &staging.ptr) == S_OK);
        context.ptr->CopyResource(staging.ptr, texture.ptr);
        D3D11_MAPPED_SUBRESOURCE mapped{};
        REQUIRE(context.ptr->Map(staging.ptr, 0, D3D11_MAP_READ, 0, &mapped) == S_OK);
        nozzle::mapped_pixels pixels{};
        pixels.data = mapped.pData;
        pixels.row_stride_bytes = mapped.RowPitch;
        pixels.width = desc.Width;
        pixels.height = desc.Height;
        pixels.format = format;
        const bool matched = transfer_pattern(pixels, seed, false);
        context.ptr->Unmap(staging.ptr, 0);
        return matched;
    }
};

} // namespace

TEST_CASE("D3D11 writable lease: outstanding reservations retain their own metadata",
          "[.][integration][d3d11][writable_lease]") {
    auto sender = make_sender(2);
    auto first = sender.acquire_writable_frame(texture_desc());
    REQUIRE(first.ok());
    write_pixels(first.value(), 6);
    auto other_desc = texture_desc();
    other_desc.width = 7;
    other_desc.height = 5;
    auto second = sender.acquire_writable_frame(other_desc);
    REQUIRE(second.ok());
    REQUIRE(sender.commit_frame(first.value()).ok());
    nozzle::receiver_desc desc{};
    desc.name = sender.info().name;
    auto receiver = nozzle::receiver::create(desc);
    REQUIRE(receiver.ok());
    nozzle::acquire_desc acquire{};
    acquire.timeout_ms = 100;
    auto received = receiver.value().acquire_frame(acquire);
    INFO(received.error().message);
    REQUIRE(received.ok());
    CHECK(received.value().info().width == width);
    CHECK(received.value().info().height == height);
    CHECK(matches_pixels(received.value(), 6));
    REQUIRE(sender.discard_frame(second.value()).ok());
}

TEST_CASE("D3D11 writable lease: external publication and foreign reservations are rejected",
          "[.][integration][d3d11][writable_lease]") {
    auto owner = make_sender();
    auto other = make_sender();
    auto writable = owner.acquire_writable_frame(texture_desc());
    REQUIRE(writable.ok());
    auto other_writable = other.acquire_writable_frame(texture_desc());
    REQUIRE(other_writable.ok());
    auto external = owner.publish_external_texture(writable.value().get_texture());
    REQUIRE_FALSE(external.ok());
    CHECK(external.error().code == nozzle::ErrorCode::UnsupportedBackend);
    auto foreign_commit = other.commit_frame(writable.value());
    REQUIRE_FALSE(foreign_commit.ok());
    CHECK(foreign_commit.error().code == nozzle::ErrorCode::InvalidArgument);
    auto foreign_discard = other.discard_frame(writable.value());
    REQUIRE_FALSE(foreign_discard.ok());
    CHECK(foreign_discard.error().code == nozzle::ErrorCode::InvalidArgument);
    CHECK(writable.value().valid());
    write_pixels(writable.value(), 7);
    REQUIRE(owner.commit_frame(writable.value()).ok());
    REQUIRE(other.discard_frame(other_writable.value()).ok());
}

TEST_CASE("D3D11 writable lease: rewrite and discard invalidate an old ring1 publication",
          "[.][integration][d3d11][writable_lease]") {
    auto sender = make_sender();
    publish(sender, 1);
    nozzle::receiver_desc desc{};
    desc.name = sender.info().name;
    auto created = nozzle::receiver::create(desc);
    REQUIRE(created.ok());
    auto receiver = std::move(created.value());
    const auto original_id = receiver.connected_info().frame_counter;
    REQUIRE(original_id > 0);
    // This receiver has not consumed A. Reusing its ring slot must retract A
    // before B changes the pixels, even if B is subsequently discarded.
    auto rewrite = sender.acquire_writable_frame(texture_desc());
    REQUIRE(rewrite.ok());
    write_pixels(rewrite.value(), 2);
    REQUIRE(sender.discard_frame(rewrite.value()).ok());
    nozzle::acquire_desc poll{};
    poll.timeout_ms = 0;
    const auto start = steady_clock::now();
    auto stale = receiver.acquire_frame(poll);
    const double duration = elapsed_ms(start);
    if (stale.ok()) {
        const auto leaked_id = stale.value().info().frame_index;
        const bool leaked_rewrite = matches_pixels(stale.value(), 2);
        stale.value().release();
        INFO("discard exposed frame=" << leaked_id << " original=" << original_id
             << " rewritten_pixels=" << leaked_rewrite);
        CHECK_FALSE(stale.ok());
    } else {
        CHECK(stale.error().code == nozzle::ErrorCode::Timeout);
    }
    CHECK(duration < 250.0);
    publish(sender, 3);
    poll.timeout_ms = 100;
    auto current = receiver.acquire_frame(poll);
    REQUIRE(current.ok());
    CHECK(current.value().info().frame_index > original_id);
    CHECK(matches_pixels(current.value(), 3));
}

TEST_CASE("D3D11 writable lease: replacing mutable texture cannot transfer write ownership",
          "[.][integration][d3d11][writable_lease]") {
    auto owner = make_sender();
    auto other = make_sender();
    auto writable = owner.acquire_writable_frame(texture_desc());
    auto other_writable = other.acquire_writable_frame(texture_desc());
    REQUIRE(writable.ok());
    REQUIRE(other_writable.ok());
    com_owner<ID3D11Texture2D> substituted;
    substituted.ptr = nozzle::d3d11::get_texture(other_writable.value().get_texture());
    REQUIRE(substituted.ptr != nullptr);
    substituted.ptr->AddRef();
    std::swap(writable.value().get_texture(), other_writable.value().get_texture());
    auto mapping = nozzle::lock_writable_pixels_mapping_with_origin(
        writable.value(), nozzle::texture_origin::top_left);
    REQUIRE_FALSE(mapping.ok());
    CHECK(mapping.error().code == nozzle::ErrorCode::InvalidArgument);
    auto committed = owner.commit_frame(writable.value());
    REQUIRE_FALSE(committed.ok());
    CHECK(committed.error().code == nozzle::ErrorCode::InvalidArgument);
    SECTION("restoring the actual reservation allows publication") {
        std::swap(writable.value().get_texture(), other_writable.value().get_texture());
        write_pixels(writable.value(), 9);
        REQUIRE(owner.commit_frame(writable.value()).ok());
        REQUIRE(other.discard_frame(other_writable.value()).ok());
    }
    SECTION("discard consumes mismatches without caching substituted resources") {
        auto discarded = owner.discard_frame(writable.value());
        REQUIRE_FALSE(discarded.ok());
        CHECK(discarded.error().code == nozzle::ErrorCode::InvalidArgument);
        CHECK_FALSE(writable.value().valid());
        auto discarded_other = other.discard_frame(other_writable.value());
        CHECK_FALSE(discarded_other.ok());
        auto next = owner.acquire_writable_frame(texture_desc());
        REQUIRE(next.ok());
        CHECK(nozzle::d3d11::get_texture(next.value().get_texture()) != substituted.ptr);
        REQUIRE(owner.discard_frame(next.value()).ok());
    }
}

TEST_CASE("D3D11 writable lease: independent CPU mapping retains lock beyond writable owner",
          "[.][integration][d3d11][writable_lease]") {
    auto sender = make_sender();
    auto acquired = sender.acquire_writable_frame(texture_desc());
    REQUIRE(acquired.ok());
    auto writable = std::move(acquired.value());
    imported_probe probe;
    probe.open(writable.get_texture());
    probe.check_held();
    auto mapped = nozzle::lock_writable_pixels_mapping_with_origin(writable, nozzle::texture_origin::top_left);
    INFO(mapped.error().message);
    REQUIRE(mapped.ok());
    auto mapping = std::move(mapped.value());
    REQUIRE(transfer_pattern(mapping.pixels(), 4, true));
    probe.check_held();
    writable = nozzle::writable_frame{};
    probe.check_held();

    bool sender_destroyed = false;
    SECTION("live sender cannot reuse until upload and unlock finish") {
        const auto start = steady_clock::now();
        auto premature = sender.acquire_writable_frame(texture_desc());
        const double duration = elapsed_ms(start);
        const bool accepted = premature.ok();
        if (accepted) REQUIRE(sender.discard_frame(premature.value()).ok());
        CHECK_FALSE(accepted);
        if (!accepted) CHECK(premature.error().code == nozzle::ErrorCode::Timeout);
        CHECK(duration < 250.0);
        probe.check_held();
    }
    SECTION("mapping also outlives sender destruction") {
        sender = nozzle::sender{};
        sender_destroyed = true;
        probe.check_held();
    }

    // Write after destruction too: ownership must protect the actual upload,
    // not merely keep the pointer or sender metadata alive.
    REQUIRE(transfer_pattern(mapping.pixels(), 5, true));
    const auto unlocked = mapping.unlock_checked();
    INFO(unlocked.error().message);
    REQUIRE(unlocked.ok());
    HRESULT available = static_cast<HRESULT>(WAIT_TIMEOUT);
    const auto deadline = steady_clock::now() + std::chrono::seconds(1);
    do {
        available = probe.acquire();
        if (available != static_cast<HRESULT>(WAIT_TIMEOUT)) break;
        Sleep(1);
    } while (steady_clock::now() < deadline);
    CHECK(available == S_OK);
    if (available == S_OK) {
        CHECK(probe.read_pattern(5));
        probe.release_checked();
    }
    if (!sender_destroyed) {
        auto recovered = sender.acquire_writable_frame(texture_desc());
        INFO(recovered.error().message);
        CHECK(recovered.ok());
        if (recovered.ok()) REQUIRE(sender.discard_frame(recovered.value()).ok());
    }
}
