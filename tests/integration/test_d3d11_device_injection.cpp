// nozzle - test_d3d11_device_injection.cpp - D3D11 native device injection + publish_native_texture

#if NOZZLE_HAS_D3D11

#include <catch2/catch_test_macros.hpp>

#include <nozzle/nozzle.hpp>
#include <nozzle/nozzle_c.h>
#include <nozzle/backends/d3d11.hpp>
#include "backends/d3d11/d3d11_helpers.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>

#include <cstdio>
#include <string>
#include <utility>

static bool create_device(D3D_DRIVER_TYPE type, ID3D11Device **device, ID3D11DeviceContext **context) {
	D3D_FEATURE_LEVEL feature_level{};
	HRESULT hr = D3D11CreateDevice(
		nullptr, type, nullptr,
		D3D11_CREATE_DEVICE_BGRA_SUPPORT,
		nullptr, 0, D3D11_SDK_VERSION,
		device, &feature_level, context);
	return SUCCEEDED(hr);
}

static bool create_hardware_device(ID3D11Device **device, ID3D11DeviceContext **context) {
	return create_device(D3D_DRIVER_TYPE_HARDWARE, device, context);
}

static bool create_warp_device(ID3D11Device **device, ID3D11DeviceContext **context) {
	return create_device(D3D_DRIVER_TYPE_WARP, device, context);
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


static ID3D11Texture2D *create_staging_bgra8_texture(ID3D11Device *device, uint32_t w, uint32_t h) {
	D3D11_TEXTURE2D_DESC desc{};
	desc.Width = w;
	desc.Height = h;
	desc.MipLevels = 1;
	desc.ArraySize = 1;
	desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	desc.SampleDesc.Count = 1;
	desc.Usage = D3D11_USAGE_STAGING;
	desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	ID3D11Texture2D *texture = nullptr;
	HRESULT hr = device->CreateTexture2D(&desc, nullptr, &texture);
	return SUCCEEDED(hr) ? texture : nullptr;
}


TEST_CASE("D3D11: NT shared handle opens on another device and preserves keyed mutex contract", "[d3d11][shared_handle][sync]") {
	ID3D11Device *sender_device = nullptr;
	ID3D11DeviceContext *sender_context = nullptr;
	REQUIRE(create_warp_device(&sender_device, &sender_context));

	ID3D11Device *receiver_device = nullptr;
	ID3D11DeviceContext *receiver_context = nullptr;
	REQUIRE(create_warp_device(&receiver_device, &receiver_context));

	auto texture_result = nozzle::d3d11::create_shared_texture(
		sender_device, 4, 4, static_cast<uint32_t>(nozzle::texture_format::bgra8_unorm));
	REQUIRE(texture_result.ok());
	nozzle::texture sender_shared_texture = std::move(texture_result.value());

	ID3D11Texture2D *sender_texture = nozzle::d3d11::get_texture(sender_shared_texture);
	REQUIRE(sender_texture != nullptr);

	D3D11_TEXTURE2D_DESC sender_desc{};
	sender_texture->GetDesc(&sender_desc);
	REQUIRE((sender_desc.MiscFlags & D3D11_RESOURCE_MISC_SHARED_NTHANDLE) != 0);
	REQUIRE((sender_desc.MiscFlags & D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX) != 0);

	uint32_t pixels[16]{};
	for (uint32_t i = 0; i < 16; ++i) {
		pixels[i] = 0xFF3366CCu;
	}

	IDXGIKeyedMutex *sender_mutex = nullptr;
	HRESULT hr = sender_texture->QueryInterface(
		__uuidof(IDXGIKeyedMutex), reinterpret_cast<void **>(&sender_mutex));
	REQUIRE(SUCCEEDED(hr));
	REQUIRE(sender_mutex != nullptr);
	hr = sender_mutex->AcquireSync(0, 1000);
	REQUIRE(hr == S_OK);
	sender_context->UpdateSubresource(sender_texture, 0, nullptr, pixels, 4 * sizeof(uint32_t), 0);
	sender_context->Flush();
	hr = sender_mutex->ReleaseSync(1);
	REQUIRE(hr == S_OK);
	sender_mutex->Release();

	HANDLE sender_handle = nozzle::d3d11::get_shared_handle(sender_shared_texture);
	REQUIRE(sender_handle != nullptr);

	auto opened_result = nozzle::d3d11::lookup_shared_texture(
		receiver_device,
		reinterpret_cast<uint64_t>(sender_handle),
		4,
		4,
		static_cast<uint32_t>(nozzle::texture_format::bgra8_unorm),
		static_cast<uint32_t>(nozzle::texture_format::bgra8_unorm),
		static_cast<uint64_t>(GetCurrentProcessId()));
	REQUIRE(opened_result.ok());
	nozzle::texture receiver_shared_texture = std::move(opened_result.value());
	ID3D11Texture2D *receiver_texture = nozzle::d3d11::get_texture(receiver_shared_texture);
	REQUIRE(receiver_texture != nullptr);

	REQUIRE(nozzle::d3d11::wait_for_slot(receiver_texture, 0, 1000));

	ID3D11Texture2D *staging = create_staging_bgra8_texture(receiver_device, 4, 4);
	REQUIRE(staging != nullptr);
	receiver_context->CopyResource(staging, receiver_texture);
	receiver_context->Flush();

	D3D11_MAPPED_SUBRESOURCE mapped{};
	hr = receiver_context->Map(staging, 0, D3D11_MAP_READ, 0, &mapped);
	REQUIRE(SUCCEEDED(hr));
	const auto *row = static_cast<const uint32_t *>(mapped.pData);
	REQUIRE(row[0] == 0xFF3366CCu);
	receiver_context->Unmap(staging, 0);
	staging->Release();

	nozzle::d3d11::release_slot(receiver_texture, 0);
	REQUIRE_FALSE(nozzle::d3d11::wait_for_slot(receiver_texture, 0, 1));

	nozzle::d3d11::signal_slot_ready(sender_texture, 0);
	REQUIRE(nozzle::d3d11::wait_for_slot(receiver_texture, 0, 1000));
	nozzle::d3d11::release_slot(receiver_texture, 0);

	receiver_context->Release();
	receiver_device->Release();
	sender_context->Release();
	sender_device->Release();
}


static std::string quote_arg(const std::string &arg) {
	std::string quoted = "\"";
	for (char ch : arg) {
		if (ch == '"') {
			quoted += "\\\"";
		} else {
			quoted += ch;
		}
	}
	quoted += "\"";
	return quoted;
}

static std::string current_executable_directory() {
	char path[MAX_PATH]{};
	DWORD len = GetModuleFileNameA(nullptr, path, MAX_PATH);
	if (len == 0 || len >= MAX_PATH) {
		return {};
	}
	std::string result(path, len);
	size_t pos = result.find_last_of("\\/");
	if (pos == std::string::npos) {
		return {};
	}
	return result.substr(0, pos + 1);
}

static std::string make_temp_ready_path(const char *prefix) {
	char temp_dir[MAX_PATH]{};
	DWORD len = GetTempPathA(MAX_PATH, temp_dir);
	if (len == 0 || len >= MAX_PATH) {
		return {};
	}
	char path[MAX_PATH]{};
	std::snprintf(path, sizeof(path), "%s%s_%lu_%lu.txt", temp_dir, prefix,
		static_cast<unsigned long>(GetCurrentProcessId()),
		static_cast<unsigned long>(GetTickCount()));
	return path;
}

static bool file_exists(const std::string &path) {
	DWORD attrs = GetFileAttributesA(path.c_str());
	return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

static bool wait_for_file(const std::string &path, uint32_t timeout_ms) {
	DWORD start = GetTickCount();
	while (GetTickCount() - start < timeout_ms) {
		if (file_exists(path)) {
			return true;
		}
		Sleep(25);
	}
	return false;
}

static uint64_t read_handle_file(const std::string &path) {
	FILE *file = std::fopen(path.c_str(), "r");
	if (!file) {
		return 0;
	}
	unsigned long long value = 0;
	std::fscanf(file, "%llu", &value);
	std::fclose(file);
	return static_cast<uint64_t>(value);
}

struct child_process {
	PROCESS_INFORMATION info{};

	~child_process() {
		if (info.hProcess) {
			TerminateProcess(info.hProcess, 0);
			WaitForSingleObject(info.hProcess, 2000);
			CloseHandle(info.hProcess);
		}
		if (info.hThread) {
			CloseHandle(info.hThread);
		}
	}

	child_process() = default;
	child_process(const child_process &) = delete;
	child_process &operator=(const child_process &) = delete;
};

static bool start_helper(const std::string &arguments, child_process &child) {
	std::string exe = current_executable_directory() + "nozzle_d3d11_cross_process_sender.exe";
	std::string command = quote_arg(exe) + " " + arguments;
	STARTUPINFOA startup{};
	startup.cb = sizeof(startup);
	BOOL ok = CreateProcessA(
		nullptr,
		&command[0],
		nullptr,
		nullptr,
		FALSE,
		0,
		nullptr,
		nullptr,
		&startup,
		&child.info);
	return ok != FALSE;
}

TEST_CASE("D3D11: NT shared handle duplicates from another process", "[d3d11][shared_handle][cross_process]") {
	std::string ready_file = make_temp_ready_path("nozzle_raw_d3d11");
	REQUIRE_FALSE(ready_file.empty());
	DeleteFileA(ready_file.c_str());

	child_process child;
	REQUIRE(start_helper(std::string("raw ") + quote_arg(ready_file), child));
	REQUIRE(wait_for_file(ready_file, 10000));

	uint64_t sender_handle = read_handle_file(ready_file);
	REQUIRE(sender_handle != 0);

	ID3D11Device *receiver_device = nullptr;
	ID3D11DeviceContext *receiver_context = nullptr;
	REQUIRE(create_warp_device(&receiver_device, &receiver_context));

	auto opened_result = nozzle::d3d11::lookup_shared_texture(
		receiver_device,
		sender_handle,
		4,
		4,
		static_cast<uint32_t>(nozzle::texture_format::bgra8_unorm),
		static_cast<uint32_t>(nozzle::texture_format::bgra8_unorm),
		static_cast<uint64_t>(child.info.dwProcessId));
	REQUIRE(opened_result.ok());
	nozzle::texture receiver_shared_texture = std::move(opened_result.value());
	ID3D11Texture2D *receiver_texture = nozzle::d3d11::get_texture(receiver_shared_texture);
	REQUIRE(receiver_texture != nullptr);

	REQUIRE(nozzle::d3d11::wait_for_slot(receiver_texture, 0, 1000));

	ID3D11Texture2D *staging = create_staging_bgra8_texture(receiver_device, 4, 4);
	REQUIRE(staging != nullptr);
	receiver_context->CopyResource(staging, receiver_texture);
	receiver_context->Flush();

	D3D11_MAPPED_SUBRESOURCE mapped{};
	HRESULT hr = receiver_context->Map(staging, 0, D3D11_MAP_READ, 0, &mapped);
	REQUIRE(SUCCEEDED(hr));
	const auto *row = static_cast<const uint32_t *>(mapped.pData);
	REQUIRE(row[0] == 0xFF3366CCu);
	receiver_context->Unmap(staging, 0);
	staging->Release();

	nozzle::d3d11::release_slot(receiver_texture, 0);

	receiver_context->Release();
	receiver_device->Release();
	DeleteFileA(ready_file.c_str());
}

TEST_CASE("D3D11: public receiver acquires frame from another process", "[d3d11][shared_handle][cross_process]") {
	std::string ready_file = make_temp_ready_path("nozzle_public_d3d11");
	REQUIRE_FALSE(ready_file.empty());
	DeleteFileA(ready_file.c_str());

	char sender_name_buffer[96]{};
	std::snprintf(sender_name_buffer, sizeof(sender_name_buffer), "d3d11_public_%lu_%lu",
		static_cast<unsigned long>(GetCurrentProcessId()),
		static_cast<unsigned long>(GetTickCount()));
	std::string sender_name(sender_name_buffer);

	child_process child;
	REQUIRE(start_helper(std::string("public ") + quote_arg(sender_name) + " " + quote_arg(ready_file), child));
	REQUIRE(wait_for_file(ready_file, 10000));

	nozzle::receiver_desc receiver_desc{};
	receiver_desc.name = sender_name;
	receiver_desc.application_name = "d3d11_cross_process_receiver";
	auto receiver_result = nozzle::receiver::create(receiver_desc);
	REQUIRE(receiver_result.ok());
	auto receiver = std::move(receiver_result.value());

	nozzle::acquire_desc acquire_desc{};
	acquire_desc.timeout_ms = 1000;
	auto frame_result = receiver.acquire_frame(acquire_desc);
	REQUIRE(frame_result.ok());
	auto frame = std::move(frame_result.value());
	REQUIRE(frame.valid());
	REQUIRE(frame.info().width == 4);
	REQUIRE(frame.info().height == 4);
	REQUIRE(frame.info().format == nozzle::texture_format::bgra8_unorm);

	DeleteFileA(ready_file.c_str());
}

// --- C++ API: device injection (WARP, deterministic) ---

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

// --- C++ API: publish_native_texture (hardware required for shared textures) ---

TEST_CASE("D3D11 C++ API: publish_native_texture with injected device", "[d3d11][native_device][publish]") {
	ID3D11Device *device = nullptr;
	ID3D11DeviceContext *context = nullptr;
	if (!create_hardware_device(&device, &context)) {
		SKIP("No D3D11 hardware device available (publish_native_texture requires hardware GPU for shared textures)");
	}

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

// --- C API: device injection (WARP, deterministic) ---

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

// --- C API: publish_native_texture (hardware required) ---

TEST_CASE("D3D11 C API: publish_native_texture with injected device", "[d3d11][native_device][publish]") {
	ID3D11Device *device = nullptr;
	ID3D11DeviceContext *context = nullptr;
	if (!create_hardware_device(&device, &context)) {
		SKIP("No D3D11 hardware device available (publish_native_texture requires hardware GPU for shared textures)");
	}

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

// --- Ownership (WARP, deterministic) ---

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
