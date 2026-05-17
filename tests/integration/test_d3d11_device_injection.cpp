// nozzle - test_d3d11_device_injection.cpp - D3D11 native device injection + publish_native_texture

#if NOZZLE_HAS_D3D11

#include <catch2/catch_test_macros.hpp>

#include <nozzle/nozzle_c.h>

#define WIN32_LEAN_AND_MEAN
#include <d3d11.h>

TEST_CASE("D3D11: create sender with injected hardware device", "[d3d11][native_device]") {
	ID3D11Device *device = nullptr;
	ID3D11DeviceContext *context = nullptr;
	D3D_FEATURE_LEVEL feature_level{};

	HRESULT hr = D3D11CreateDevice(
		nullptr, D3D11_DRIVER_TYPE_HARDWARE, nullptr,
		0, nullptr, 0, D3D11_SDK_VERSION,
		&device, &feature_level, &context);

	if (FAILED(hr)) {
		SKIP("No D3D11 hardware device available");
	}

	NozzleSenderDesc desc{};
	desc.name = "test_d3d11_native";
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

TEST_CASE("D3D11: publish_native_texture with injected device", "[d3d11][native_device][publish]") {
	ID3D11Device *device = nullptr;
	ID3D11DeviceContext *context = nullptr;
	D3D_FEATURE_LEVEL feature_level{};

	HRESULT hr = D3D11CreateDevice(
		nullptr, D3D11_DRIVER_TYPE_HARDWARE, nullptr,
		0, nullptr, 0, D3D11_SDK_VERSION,
		&device, &feature_level, &context);

	if (FAILED(hr)) {
		SKIP("No D3D11 hardware device available");
	}

	NozzleSenderDesc desc{};
	desc.name = "test_d3d11_publish";
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

	D3D11_TEXTURE2D_DESC tex_desc{};
	tex_desc.Width = 64;
	tex_desc.Height = 64;
	tex_desc.MipLevels = 1;
	tex_desc.ArraySize = 1;
	tex_desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	tex_desc.SampleDesc.Count = 1;
	tex_desc.Usage = D3D11_USAGE_DEFAULT;
	tex_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

	ID3D11Texture2D *texture = nullptr;
	hr = device->CreateTexture2D(&tex_desc, nullptr, &texture);
	REQUIRE(SUCCEEDED(hr));
	REQUIRE(texture != nullptr);

	ec = nozzle_sender_publish_native_texture(
		sender, texture, 64, 64, NOZZLE_FORMAT_BGRA8_UNORM);
	REQUIRE(ec == NOZZLE_OK);

	texture->Release();
	nozzle_sender_destroy(sender);
	context->Release();
	device->Release();
}

TEST_CASE("D3D11: destruction with injected device does not release caller resource", "[d3d11][native_device]") {
	ID3D11Device *device = nullptr;
	ID3D11DeviceContext *context = nullptr;
	D3D_FEATURE_LEVEL feature_level{};

	HRESULT hr = D3D11CreateDevice(
		nullptr, D3D11_DRIVER_TYPE_HARDWARE, nullptr,
		0, nullptr, 0, D3D11_SDK_VERSION,
		&device, &feature_level, &context);

	if (FAILED(hr)) {
		SKIP("No D3D11 hardware device available");
	}

	{
		NozzleSenderDesc desc{};
		desc.name = "test_d3d11_destroy";
		desc.application_name = "test";
		desc.ring_buffer_size = 2;

		NozzleNativeDevice native_dev{};
		native_dev.backend = NOZZLE_BACKEND_D3D11;
		native_dev.device = device;
		native_dev.context = context;

		NozzleSender *sender = nullptr;
		NozzleErrorCode ec = nozzle_sender_create_with_native_device(&desc, &native_dev, &sender);
		REQUIRE(ec == NOZZLE_OK);

		nozzle_sender_destroy(sender);
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
	hr = device->CreateTexture2D(&tex_desc, nullptr, &texture);
	REQUIRE(SUCCEEDED(hr));
	texture->Release();

	context->Release();
	device->Release();
}

#endif
