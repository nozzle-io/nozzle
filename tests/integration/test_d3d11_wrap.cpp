#if NOZZLE_HAS_D3D11

#include <catch2/catch_test_macros.hpp>
#include <nozzle/backends/d3d11.hpp>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

namespace {

Microsoft::WRL::ComPtr<ID3D11Device> make_device() {
    Microsoft::WRL::ComPtr<ID3D11Device> device;
    D3D_FEATURE_LEVEL level{};
    REQUIRE(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr,
        D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION,
        device.GetAddressOf(), &level, nullptr)));
    return device;
}

Microsoft::WRL::ComPtr<ID3D11Texture2D> make_texture(ID3D11Device *device,
                                                  DXGI_FORMAT format, bool shared = true) {
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = 7;
    desc.Height = 3;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = format;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    desc.MiscFlags = shared
        ? D3D11_RESOURCE_MISC_SHARED_NTHANDLE | D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX : 0;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
    REQUIRE(SUCCEEDED(device->CreateTexture2D(&desc, nullptr, texture.GetAddressOf())));
    return texture;
}

nozzle::d3d11::TextureWrapDesc wrap_desc(ID3D11Texture2D *texture, DXGI_FORMAT format) {
    nozzle::d3d11::TextureWrapDesc desc{};
    desc.texture = texture;
    desc.dxgi_format = static_cast<uint32_t>(format);
    desc.width = 7;
    desc.height = 3;
    return desc;
}

} // namespace

TEST_CASE("D3D11 wrap translates native format and preserves caller ownership",
          "[d3d11][native_wrap]") {
    auto device = make_device();
    struct format_case {
        DXGI_FORMAT native;
        nozzle::texture_format storage;
        nozzle::channel_order order;
    };
    const format_case cases[]{
        {DXGI_FORMAT_R8G8B8A8_UNORM, nozzle::texture_format::rgba8_unorm, nozzle::channel_order::rgba},
        {DXGI_FORMAT_B8G8R8A8_UNORM, nozzle::texture_format::bgra8_unorm, nozzle::channel_order::bgra},
    };
    for (const auto &expected : cases) {
        DYNAMIC_SECTION("DXGI " << static_cast<uint32_t>(expected.native)) {
            auto original = make_texture(device.Get(), expected.native);
            HANDLE shared_handle = nullptr;
            {
                auto result = nozzle::d3d11::wrap_texture(wrap_desc(original.Get(), expected.native));
                REQUIRE(result.ok());
                const auto &texture = result.value();
                REQUIRE(texture.valid());
                CHECK(nozzle::d3d11::get_texture(texture) == original.Get());
                CHECK(texture.desc().width == 7);
                CHECK(texture.desc().height == 3);
                CHECK(texture.desc().format == expected.storage);
                CHECK(texture.desc().semantic_format == expected.storage);
                CHECK(texture.desc().swizzle == nozzle::channel_swizzle::identity);
                const auto &resolved = texture.resolved();
                CHECK(resolved.storage_format == expected.storage);
                CHECK(resolved.semantic_format == expected.storage);
                CHECK(resolved.swizzle == nozzle::channel_swizzle::identity);
                CHECK(resolved.native.backend == nozzle::backend_type::d3d11);
                CHECK(resolved.native.kind == nozzle::native_format_kind::dxgi_format);
                CHECK(resolved.native.value == static_cast<uint32_t>(expected.native));
                CHECK(resolved.source == nozzle::format_source::native_observed);
                CHECK(resolved.cpu_layout.order == expected.order);
                CHECK(resolved.cpu_layout.component == nozzle::component_type::unorm);
                CHECK(resolved.cpu_layout.component_bits == 8);
                CHECK(resolved.cpu_layout.channel_count == 4);
                CHECK(resolved.cpu_layout.bytes_per_pixel == 4);
                CHECK(resolved.sampling.normalized);
                CHECK_FALSE(resolved.sampling.srgb);
                CHECK_FALSE(resolved.sampling.integer);
                CHECK_FALSE(resolved.sampling.floating_point);
                CHECK_FALSE(resolved.sampling.depth);
                shared_handle = nozzle::d3d11::get_shared_handle(texture);
                REQUIRE(shared_handle != nullptr);
                DWORD flags{};
                REQUIRE(GetHandleInformation(shared_handle, &flags));
            }
            DWORD flags{};
            CHECK_FALSE(GetHandleInformation(shared_handle, &flags));
            CHECK(GetLastError() == ERROR_INVALID_HANDLE);
            D3D11_TEXTURE2D_DESC observed{};
            original->GetDesc(&observed);
            CHECK(observed.Format == expected.native);
            CHECK(observed.Width == 7);
            CHECK(observed.Height == 3);
        }
    }
}

TEST_CASE("D3D11 wrap rejects hints before consuming shared handle creation",
          "[d3d11][native_wrap]") {
    auto device = make_device();
    for (int input = 0; input < 5; ++input) {
        DYNAMIC_SECTION("invalid declaration " << input) {
            auto original = make_texture(device.Get(), DXGI_FORMAT_B8G8R8A8_UNORM);
            auto desc = wrap_desc(original.Get(), DXGI_FORMAT_B8G8R8A8_UNORM);
            if (input == 0) desc.dxgi_format = DXGI_FORMAT_R8G8B8A8_UNORM;
            if (input == 1) desc.dxgi_format = DXGI_FORMAT_UNKNOWN;
            if (input == 2) desc.width = 0;
            if (input == 3) desc.width = 8;
            if (input == 4) desc.height = 4;
            {
                auto rejected = nozzle::d3d11::wrap_texture(desc);
                CHECK_FALSE(rejected.ok());
                if (!rejected.ok()) CHECK(rejected.error().code == nozzle::ErrorCode::InvalidArgument);
            }
            // CreateSharedHandle is one-shot: invalid hints must not consume it.
            auto corrected = nozzle::d3d11::wrap_texture(
                wrap_desc(original.Get(), DXGI_FORMAT_B8G8R8A8_UNORM));
            REQUIRE(corrected.ok());
        }
    }
}

TEST_CASE("D3D11 wrap rejects null and unsupported native inputs",
          "[d3d11][native_wrap]") {
    auto null_result = nozzle::d3d11::wrap_texture({});
    REQUIRE_FALSE(null_result.ok());
    CHECK(null_result.error().code == nozzle::ErrorCode::InvalidArgument);
    auto device = make_device();
    // Unsupported format is rejected even before a non-shareable resource
    // reaches CreateSharedHandle, which would report a different error.
    auto original = make_texture(device.Get(), DXGI_FORMAT_R8G8B8A8_SNORM, false);
    auto unsupported = nozzle::d3d11::wrap_texture(
        wrap_desc(original.Get(), DXGI_FORMAT_R8G8B8A8_SNORM));
    REQUIRE_FALSE(unsupported.ok());
    CHECK(unsupported.error().code == nozzle::ErrorCode::UnsupportedFormat);
    D3D11_TEXTURE2D_DESC observed{};
    original->GetDesc(&observed);
    CHECK(observed.Format == DXGI_FORMAT_R8G8B8A8_SNORM);
}

#endif
