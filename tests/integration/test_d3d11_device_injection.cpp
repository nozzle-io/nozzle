// nozzle - test_d3d11_device_injection.cpp - D3D11 native device injection + publish_native_texture

#if NOZZLE_HAS_D3D11

#include <catch2/catch_test_macros.hpp>

#include <nozzle/nozzle.hpp>
#include <nozzle/nozzle_c.h>

#define WIN32_LEAN_AND_MEAN
#include <d3d11.h>

static bool create_warp_device(ID3D11Device **device, ID3D11DeviceContext **context) {
	D3D_FEATURE_LEVEL feature_level{};
	HRESULT hr = D3D11CreateDevice(
		nullptr, D3D_DRIVER_TYPE_WARP, nullptr,
		D3D11_CREATE_DEVICE_BGRA_SUPPORT,
		nullptr, 0, D3D11_SDK_VERSION,
		device, &feature_level, context);
	return SUCCEEDED(hr);
}

static ID3D11Texture2D *create_bgra8_texture(ID3D11Device *device, uint32_t w, uint32_t h) {
	D3D11_TEXTURE2D_DESC desc{};
	desc.Width = w;
	desc.Height = h;
	desc.MipLevels = 1;
	desc.ArraySize = 1;
	desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	desc.SampleDesc.Count = 1;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	ID3D11Texture2D *texture = nullptr;
	HRESULT hr = device->CreateTexture2D(&desc, nullptr, &texture);
	return SUCCEEDED(hr) ? texture : nullptr;
}

// --- C++ API tests ---

TEST_CASE("D3D11 C++ API: create sender with injected WARP device", "[d3d11][native_device]") {
	ID3D11Device *device = nullptr;
	ID3D11DeviceContext *context = nullptr;
	REQUIRE(create_warp_device(&device, &context));

	nozzle::sender_desc desc{};
	desc.name = "test_cpp_d3d11_warp";
	desc.application_name = "test";
	desc.ring_buffer_size = 2;
	desc.native_device = nozzle::native_device_desc{
		nozzle::backend_type::d3d11, device, context
	};

	auto sender_result = nozzle::sender::create(desc);
	REQUIRE(sender_result.ok());

	auto nd = sender_result.value().native_device();
	REQUIRE(nd.device == device);
	REQUIRE(nd.backend == nozzle::backend_type::d3d11);

	context->Release();
	device->Release();
}

TEST_CASE("D3D11 C++ API: publish_native_texture with injected WARP device", "[d3d11][native_device][publish]") {
	ID3D11Device *device = nullptr;
	ID3D11DeviceContext *context = nullptr;
	REQUIRE(create_warp_device(&device, &context));

	nozzle::sender_desc desc{};
	desc.name = "test_cpp_d3d11_publish";
	desc.application_name = "test";
	desc.ring_buffer_size = 2;
	desc.native_device = nozzle::native_device_desc{
		nozzle::backend_type::d3d11, device, context
	};

	auto sender_result = nozzle::sender::create(desc);
	REQUIRE(sender_result.ok());

	auto *texture = create_bgra8_texture(device, 64, 64);
	REQUIRE(texture != nullptr);

	auto pub_result = sender_result.value().publish_native_texture(
		texture, 64, 64, nozzle::texture_format::bgra8_unorm);
	REQUIRE(pub_result.ok());

	texture->Release();
	context->Release();
	device->Release();
}

// --- C API tests ---

TEST_CASE("D3D11 C API: create sender with injected WARP device", "[d3d11][native_device]") {
	ID3D11Device *device = nullptr;
	ID3D11DeviceContext *context = nullptr;
	REQUIRE(create_warp_device(&device, &context));

	NozzleSenderDesc desc{};
	desc.name = "test_c_d3d11_warp";
	desc.application_name = "test";
	desc.ring_buffer_size = 2;

	NozzleNativeDevice native_dev{};
	native_dev.backend = NOZZLE_BACKEND_D3D11;
	native_dev.device = device;
	native_dev.context = context;

	NozzleSender *sender = nullptr;
	NozzleErrorCode ec = nozzle_sender_create_with_native_device(&desc, &native_dev, &sender);
	REQUIRE(ec == NOZZLE_OK);
	REQUIRE(sender != nullptr);

	NozzleNativeDevice queried{};
	ec = nozzle_sender_get_native_device(sender, &queried);
	REQUIRE(ec == NOZZLE_OK);
	REQUIRE(queried.device == device);
	REQUIRE(queried.backend == NOZZLE_BACKEND_D3D11);

	nozzle_sender_destroy(sender);
	context->Release();
	device->Release();
}

TEST_CASE("D3D11 C API: publish_native_texture with injected WARP device", "[d3d11][native_device][publish]") {
	ID3D11Device *device = nullptr;
	ID3D11DeviceContext *context = nullptr;
	REQUIRE(create_warp_device(&device, &context));

	NozzleSenderDesc desc{};
	desc.name = "test_c_d3d11_publish";
	desc.application_name = "test";
	desc.ring_buffer_size = 2;

	NozzleNativeDevice native_dev{};
	native_dev.backend = NOZZLE_BACKEND_D3D11;
	native_dev.device = device;
	native_dev.context = context;

	NozzleSender *sender = nullptr;
	NozzleErrorCode ec = nozzle_sender_create_with_native_device(&desc, &native_dev, &sender);
	REQUIRE(ec == NOZZLE_OK);
	REQUIRE(sender != nullptr);

	auto *texture = create_bgra8_texture(device, 64, 64);
	REQUIRE(texture != nullptr);

	ec = nozzle_sender_publish_native_texture(
		sender, texture, 64, 64, NOZZLE_FORMAT_BGRA8_UNORM);
	REQUIRE(ec == NOZZLE_OK);

	texture->Release();
	nozzle_sender_destroy(sender);
	context->Release();
	device->Release();
}

// --- Ownership test ---

TEST_CASE("D3D11: destruction with injected device does not release caller resource", "[d3d11][native_device]") {
	ID3D11Device *device = nullptr;
	ID3D11DeviceContext *context = nullptr;
	REQUIRE(create_warp_device(&device, &context));

	{
		nozzle::sender_desc desc{};
		desc.name = "test_d3d11_destroy";
		desc.application_name = "test";
		desc.ring_buffer_size = 2;
		desc.native_device = nozzle::native_device_desc{
			nozzle::backend_type::d3d11, device, context
		};

		auto sender_result = nozzle::sender::create(desc);
		REQUIRE(sender_result.ok());
	}

	// Device and context must still be valid after sender destruction
	// (nozzle does not own injected devices)
	D3D11_TEXTURE2D_DESC tex_desc{};
	tex_desc.Width = 4;
	tex_desc.Height = 4;
	tex_desc.MipLevels = 1;
	tex_desc.ArraySize = 1;
	tex_desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	tex_desc.SampleDesc.Count = 1;
	tex_desc.Usage = D3D11_USAGE_DEFAULT;

	ID3D11Texture2D *texture = nullptr;
	HRESULT hr = device->CreateTexture2D(&tex_desc, nullptr, &texture);
	REQUIRE(SUCCEEDED(hr));
	texture->Release();

	context->Release();
	device->Release();
}

#endif
