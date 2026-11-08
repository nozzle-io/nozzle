// nozzle - test_d3d11_device_injection.cpp - D3D11 native device injection + publish_native_texture

#if NOZZLE_HAS_D3D11

#include <catch2/catch_test_macros.hpp>

#include <nozzle/nozzle.hpp>
#include <nozzle/nozzle_c.h>
#include <nozzle/backends/d3d11.hpp>
#include <nozzle/pixel_access.hpp>
#include "backends/d3d11/d3d11_helpers.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <utility>
#include <vector>

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

static void fill_bgra8_texture(ID3D11DeviceContext *context, ID3D11Texture2D *texture, uint32_t pixel) {
	D3D11_TEXTURE2D_DESC desc{};
	texture->GetDesc(&desc);
	uint32_t pixels[32]{};
	REQUIRE(desc.Width * desc.Height <= 32);
	for (uint32_t i = 0; i < desc.Width * desc.Height; ++i) {
		pixels[i] = pixel;
	}
	context->UpdateSubresource(texture, 0, nullptr, pixels, desc.Width * sizeof(uint32_t), 0);
	context->Flush();
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
		sender_device, 4, 4, nozzle::texture_format::bgra8_unorm);
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
	hr = sender_mutex->ReleaseSync(0);
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
	REQUIRE(nozzle::d3d11::wait_for_slot(receiver_texture, 0, 0));
	nozzle::d3d11::release_slot(receiver_texture, 0);

	REQUIRE(nozzle::d3d11::signal_slot_ready(sender_texture, 0));
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

	auto pixels_result = nozzle::lock_frame_pixels_with_origin(frame, nozzle::texture_origin::top_left);
	REQUIRE(pixels_result.ok());
	auto pixels = pixels_result.value();
	REQUIRE(pixels.data != nullptr);
	const auto *row = static_cast<const uint32_t *>(pixels.data);
	REQUIRE(row[0] == 0xFF3366CCu);
	nozzle::unlock_frame_pixels(frame);

	DeleteFileA(ready_file.c_str());
}

TEST_CASE("C API snapshot survives concurrent cross-process sender identity transition", "[d3d11][c_api][info_snapshot][cross_process][threading]") {
	std::string ready_file = make_temp_ready_path("nozzle_snapshot_identity");
	REQUIRE_FALSE(ready_file.empty());
	DeleteFileA(ready_file.c_str());

	char sender_name_buffer[64]{};
	std::snprintf(sender_name_buffer, sizeof(sender_name_buffer), "snapshot_identity_%lu",
		static_cast<unsigned long>(GetCurrentProcessId()));
	std::string sender_name(sender_name_buffer);

	NozzleReceiver *receiver = nullptr;
	NozzleConnectedSenderSnapshot first{};
	constexpr int reader_count = 4;
	std::atomic<bool> stop{false};
	std::atomic<bool> second_identity_ready{false};
	std::atomic<int> readers_saw_first{0};
	std::atomic<int> readers_saw_disconnected{0};
	std::atomic<int> readers_saw_second{0};
	std::atomic<int> reader_errors{0};
	std::string first_id{};
	std::string second_id{};
	std::vector<std::vector<std::string>> observed_ids(reader_count);
	std::vector<std::thread> readers{};
	bool first_observed_by_all = false;
	bool disconnected_observed_by_all = false;
	{
		child_process first_child;
		REQUIRE(start_helper(std::string("public ") + quote_arg(sender_name) + " " + quote_arg(ready_file), first_child));
		REQUIRE(wait_for_file(ready_file, 10000));

		NozzleReceiverDesc receiver_desc{};
		receiver_desc.name = sender_name.c_str();
		receiver_desc.application_name = "snapshot_receiver";
		REQUIRE(nozzle_receiver_create(&receiver_desc, &receiver) == NOZZLE_OK);

		NozzleAcquireDesc acquire_desc{};
		acquire_desc.timeout_ms = 1000;
		NozzleFrame *frame = nullptr;
		REQUIRE(nozzle_receiver_acquire_frame(receiver, &acquire_desc, &frame) == NOZZLE_OK);
		nozzle_frame_release(frame);

		REQUIRE(nozzle_receiver_get_connected_info_snapshot(receiver, &first) == NOZZLE_OK);
		REQUIRE(first.connected == 1);
		REQUIRE(std::strcmp(first.name, sender_name.c_str()) == 0);
		REQUIRE(first.id[0] != '\0');
		first_id = first.id;

		for (int index = 0; index < reader_count; ++index) {
			readers.emplace_back([&, index] {
				bool saw_first = false;
				bool saw_disconnected = false;
				bool saw_second = false;
				do {
					NozzleConnectedSenderSnapshot snapshot{};
					if (nozzle_receiver_get_connected_info_snapshot(receiver, &snapshot) != NOZZLE_OK ||
						std::strcmp(snapshot.name, sender_name.c_str()) != 0 ||
						std::strcmp(snapshot.application_name, first.application_name) != 0 ||
						snapshot.id[0] == '\0' || snapshot.connected > 1) {
						reader_errors.fetch_add(1);
						continue;
					}

					std::string id(snapshot.id);
					if (snapshot.connected == 0) {
						if (id != first_id) {
							reader_errors.fetch_add(1);
							continue;
						}
						if (!saw_disconnected) {
							saw_disconnected = true;
							readers_saw_disconnected.fetch_add(1);
						}
					}
					if (observed_ids[index].empty() || observed_ids[index].back() != id) {
						observed_ids[index].push_back(id);
					}
					if (!saw_first && id == first_id) {
						saw_first = true;
						readers_saw_first.fetch_add(1);
					}
					if (!saw_second && second_identity_ready.load(std::memory_order_acquire)
						&& id == second_id) {
						saw_second = true;
						readers_saw_second.fetch_add(1);
					}
				} while (!stop.load());
			});
		}

		const auto first_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
		while (readers_saw_first.load() < reader_count &&
			std::chrono::steady_clock::now() < first_deadline) {
			std::this_thread::yield();
		}
		first_observed_by_all = readers_saw_first.load() == reader_count;
	}

	NozzleAcquireDesc closed_desc{};
	closed_desc.timeout_ms = 50;
	NozzleFrame *closed_frame = nullptr;
	const NozzleErrorCode closed_error =
		nozzle_receiver_acquire_frame(receiver, &closed_desc, &closed_frame);
	if (closed_frame != nullptr) {
		nozzle_frame_release(closed_frame);
	}
	NozzleConnectedSenderSnapshot disconnected{};
	const NozzleErrorCode disconnected_snapshot_error =
		nozzle_receiver_get_connected_info_snapshot(receiver, &disconnected);
	const bool disconnected_snapshot_valid =
		disconnected_snapshot_error == NOZZLE_OK && disconnected.connected == 0 &&
		std::strcmp(disconnected.name, first.name) == 0 &&
		std::strcmp(disconnected.application_name, first.application_name) == 0 &&
		std::strcmp(disconnected.id, first.id) == 0;
	if (disconnected_snapshot_valid) {
		const auto disconnected_deadline =
			std::chrono::steady_clock::now() + std::chrono::seconds(5);
		while (readers_saw_disconnected.load() < reader_count &&
			std::chrono::steady_clock::now() < disconnected_deadline) {
			std::this_thread::yield();
		}
		disconnected_observed_by_all =
			readers_saw_disconnected.load() == reader_count;
	}

	DeleteFileA(ready_file.c_str());
	NozzleConnectedSenderSnapshot second{};
	bool second_started = false;
	bool second_ready = false;
	NozzleErrorCode reconnect_error = NOZZLE_ERROR_UNKNOWN;
	NozzleErrorCode second_snapshot_error = NOZZLE_ERROR_UNKNOWN;
	bool second_observed_by_all = false;
	{
		child_process second_child;
		second_started = start_helper(
			std::string("public ") + quote_arg(sender_name) + " " + quote_arg(ready_file),
			second_child);
		if (second_started) {
			second_ready = wait_for_file(ready_file, 10000);
		}
		if (second_ready) {
			NozzleAcquireDesc acquire_desc{};
			acquire_desc.timeout_ms = 1000;
			NozzleFrame *frame = nullptr;
			reconnect_error = nozzle_receiver_acquire_frame(receiver, &acquire_desc, &frame);
			if (frame != nullptr) {
				nozzle_frame_release(frame);
			}
		}
		if (reconnect_error == NOZZLE_OK) {
			second_snapshot_error = nozzle_receiver_get_connected_info_snapshot(receiver, &second);
			if (second_snapshot_error == NOZZLE_OK) {
				second_id = second.id;
				second_identity_ready.store(true, std::memory_order_release);
				const auto second_deadline =
					std::chrono::steady_clock::now() + std::chrono::seconds(5);
				while (readers_saw_second.load() < reader_count &&
					std::chrono::steady_clock::now() < second_deadline) {
					std::this_thread::yield();
				}
				second_observed_by_all = readers_saw_second.load() == reader_count;
			}
		}
	}

	stop.store(true);
	for (auto &reader : readers) {
		reader.join();
	}

	INFO("first_id=" << first_id << " second_id=" << second_id
		<< " readers_saw_first=" << readers_saw_first.load()
		<< " readers_saw_disconnected=" << readers_saw_disconnected.load()
		<< " readers_saw_second=" << readers_saw_second.load()
		<< " reader_errors=" << reader_errors.load());
	REQUIRE(first_observed_by_all);
	REQUIRE(closed_error == NOZZLE_ERROR_SENDER_CLOSED);
	REQUIRE(disconnected_snapshot_error == NOZZLE_OK);
	REQUIRE(disconnected_snapshot_valid);
	REQUIRE(disconnected_observed_by_all);
	REQUIRE(second_started);
	REQUIRE(second_ready);
	REQUIRE(reconnect_error == NOZZLE_OK);
	REQUIRE(second_snapshot_error == NOZZLE_OK);
	REQUIRE(std::strcmp(first.name, sender_name.c_str()) == 0);
	REQUIRE(first_id == first.id);
	REQUIRE(std::strcmp(second.name, sender_name.c_str()) == 0);
	REQUIRE(second.connected == 1);
	REQUIRE(second.id[0] != '\0');
	REQUIRE(first_id != second_id);
	REQUIRE(second_observed_by_all);
	REQUIRE(reader_errors.load() == 0);
	for (const auto &ids : observed_ids) {
		REQUIRE(std::find(ids.begin(), ids.end(), first_id) != ids.end());
		REQUIRE(std::find(ids.begin(), ids.end(), second_id) != ids.end());
		for (const auto &id : ids) {
			REQUIRE((id == first_id || id == second_id));
		}
	}

	nozzle_receiver_destroy(receiver);
	DeleteFileA(ready_file.c_str());
}

TEST_CASE("D3D11: sender can publish past ring size without receiver", "[d3d11][shared_handle][producer]") {
	ID3D11Device *device = nullptr;
	ID3D11DeviceContext *context = nullptr;
	REQUIRE(create_warp_device(&device, &context));

	nozzle::sender_desc desc{};
	desc.name = "d3d11_producer_only";
	desc.application_name = "test";
	desc.ring_buffer_size = 3;
	desc.native_device = nozzle::native_device_desc{
		nozzle::backend_type::d3d11, device, context
	};

	auto sender_result = nozzle::sender::create(desc);
	REQUIRE(sender_result.ok());
	auto sender = std::move(sender_result.value());

	ID3D11Texture2D *source = create_bgra8_texture(device, 4, 4);
	REQUIRE(source != nullptr);

	uint32_t pixels[16]{};
	for (uint32_t i = 0; i < 16; ++i) {
		pixels[i] = 0xFF3366CCu;
	}

	for (uint32_t frame_index = 0; frame_index < 5; ++frame_index) {
		context->UpdateSubresource(source, 0, nullptr, pixels, 4 * sizeof(uint32_t), 0);
		context->Flush();
		auto publish_result = sender.publish_native_texture(
			source, 4, 4, nozzle::texture_format::bgra8_unorm);
		REQUIRE(publish_result.ok());
	}

	source->Release();
	context->Release();
	device->Release();
}

TEST_CASE("D3D11: writable commits can publish past ring size without receiver", "[d3d11][shared_handle][producer]") {
	ID3D11Device *device = nullptr;
	ID3D11DeviceContext *context = nullptr;
	REQUIRE(create_warp_device(&device, &context));

	nozzle::sender_desc desc{};
	desc.name = "d3d11_writable_producer_only";
	desc.application_name = "test";
	desc.ring_buffer_size = 3;
	desc.native_device = nozzle::native_device_desc{
		nozzle::backend_type::d3d11, device, context
	};

	auto sender_result = nozzle::sender::create(desc);
	REQUIRE(sender_result.ok());
	auto sender = std::move(sender_result.value());

	nozzle::texture_desc texture_desc{};
	texture_desc.width = 4;
	texture_desc.height = 4;
	texture_desc.format = nozzle::texture_format::bgra8_unorm;

	for (uint32_t frame_index = 0; frame_index < 5; ++frame_index) {
		auto frame_result = sender.acquire_writable_frame(texture_desc);
		REQUIRE(frame_result.ok());
		auto writable = std::move(frame_result.value());
		REQUIRE(writable.valid());

		auto pixels_result = nozzle::lock_writable_pixels_with_origin(writable, nozzle::texture_origin::top_left);
		REQUIRE(pixels_result.ok());
		auto pixels = pixels_result.value();
		REQUIRE(pixels.data != nullptr);
		for (uint32_t y = 0; y < pixels.height; ++y) {
			auto *row = reinterpret_cast<uint32_t *>(
				static_cast<uint8_t *>(pixels.data) + pixels.row_stride_bytes * y);
			for (uint32_t x = 0; x < pixels.width; ++x) {
				row[x] = 0xFF3366CCu + frame_index;
			}
		}
		nozzle::unlock_writable_pixels(writable);

		auto commit_result = sender.commit_frame(writable);
		REQUIRE(commit_result.ok());
		REQUIRE_FALSE(writable.valid());
	}

	context->Release();
	device->Release();
}

TEST_CASE("D3D11: slow receiver can drop held frame while sender continues", "[d3d11][shared_handle][producer][receiver]") {
	char sender_name_buffer[96]{};
	std::snprintf(sender_name_buffer, sizeof(sender_name_buffer), "d3d11_slow_receiver_%lu_%lu",
		static_cast<unsigned long>(GetCurrentProcessId()),
		static_cast<unsigned long>(GetTickCount()));
	std::string sender_name(sender_name_buffer);

	nozzle::sender_desc desc{};
	desc.name = sender_name;
	desc.application_name = "test";
	desc.ring_buffer_size = 3;

	auto sender_result = nozzle::sender::create(desc);
	REQUIRE(sender_result.ok());
	auto sender = std::move(sender_result.value());

	auto native = sender.native_device();
	auto *device = static_cast<ID3D11Device *>(native.device);
	REQUIRE(device != nullptr);
	ID3D11DeviceContext *context = nullptr;
	device->GetImmediateContext(&context);
	REQUIRE(context != nullptr);

	ID3D11Texture2D *source = create_bgra8_texture(device, 4, 4);
	REQUIRE(source != nullptr);

	fill_bgra8_texture(context, source, 0xFF336600u);
	auto first_publish = sender.publish_native_texture(
		source, 4, 4, nozzle::texture_format::bgra8_unorm);
	REQUIRE(first_publish.ok());

	nozzle::receiver_desc receiver_desc{};
	receiver_desc.name = sender_name;
	auto receiver_result = nozzle::receiver::create(receiver_desc);
	REQUIRE(receiver_result.ok());
	auto receiver = std::move(receiver_result.value());

	nozzle::acquire_desc acquire_desc{};
	acquire_desc.timeout_ms = 1000;
	auto held_result = receiver.acquire_frame(acquire_desc);
	REQUIRE(held_result.ok());
	auto held = std::move(held_result.value());
	REQUIRE(held.valid());
	REQUIRE(held.info().frame_index == 1);

	uint32_t expected_pixel = 0;
	for (uint32_t frame_index = 0; frame_index < 5; ++frame_index) {
		expected_pixel = 0xFF336601u + frame_index;
		fill_bgra8_texture(context, source, expected_pixel);
		auto publish_result = sender.publish_native_texture(
			source, 4, 4, nozzle::texture_format::bgra8_unorm);
		REQUIRE(publish_result.ok());
	}

	held.release();

	acquire_desc.timeout_ms = 2000;
	auto latest_result = receiver.acquire_frame(acquire_desc);
	REQUIRE(latest_result.ok());
	auto latest = std::move(latest_result.value());
	REQUIRE(latest.valid());
	REQUIRE(latest.info().frame_index == 6);

	auto pixels_result = nozzle::lock_frame_pixels_with_origin(latest, nozzle::texture_origin::top_left);
	REQUIRE(pixels_result.ok());
	auto pixels = pixels_result.value();
	REQUIRE(pixels.data != nullptr);
	const auto *row = static_cast<const uint32_t *>(pixels.data);
	REQUIRE(row[0] == expected_pixel);
	nozzle::unlock_frame_pixels(latest);

	source->Release();
	context->Release();
}

TEST_CASE("D3D11: slow receiver handles resource replacement while sender continues", "[d3d11][shared_handle][producer][receiver]") {
	char sender_name_buffer[96]{};
	std::snprintf(sender_name_buffer, sizeof(sender_name_buffer), "d3d11_resource_replace_%lu_%lu",
		static_cast<unsigned long>(GetCurrentProcessId()),
		static_cast<unsigned long>(GetTickCount()));
	std::string sender_name(sender_name_buffer);

	nozzle::sender_desc desc{};
	desc.name = sender_name;
	desc.application_name = "test";
	desc.ring_buffer_size = 3;

	auto sender_result = nozzle::sender::create(desc);
	REQUIRE(sender_result.ok());
	auto sender = std::move(sender_result.value());

	auto native = sender.native_device();
	auto *device = static_cast<ID3D11Device *>(native.device);
	REQUIRE(device != nullptr);
	ID3D11DeviceContext *context = nullptr;
	device->GetImmediateContext(&context);
	REQUIRE(context != nullptr);

	ID3D11Texture2D *source_4x4 = create_bgra8_texture(device, 4, 4);
	REQUIRE(source_4x4 != nullptr);
	fill_bgra8_texture(context, source_4x4, 0xFF110000u);
	auto first_publish = sender.publish_native_texture(
		source_4x4, 4, 4, nozzle::texture_format::bgra8_unorm);
	REQUIRE(first_publish.ok());

	nozzle::receiver_desc receiver_desc{};
	receiver_desc.name = sender_name;
	auto receiver_result = nozzle::receiver::create(receiver_desc);
	REQUIRE(receiver_result.ok());
	auto receiver = std::move(receiver_result.value());

	nozzle::acquire_desc acquire_desc{};
	acquire_desc.timeout_ms = 1000;
	auto held_result = receiver.acquire_frame(acquire_desc);
	REQUIRE(held_result.ok());
	auto held = std::move(held_result.value());
	REQUIRE(held.valid());
	REQUIRE(held.info().frame_index == 1);
	REQUIRE(held.info().width == 4);
	REQUIRE(held.info().height == 4);

	ID3D11Texture2D *source_8x4 = create_bgra8_texture(device, 8, 4);
	REQUIRE(source_8x4 != nullptr);

	uint32_t expected_pixel = 0;
	for (uint32_t frame_index = 0; frame_index < 5; ++frame_index) {
		expected_pixel = 0xFF220000u + frame_index;
		fill_bgra8_texture(context, source_8x4, expected_pixel);
		auto publish_result = sender.publish_native_texture(
			source_8x4, 8, 4, nozzle::texture_format::bgra8_unorm);
		REQUIRE(publish_result.ok());
	}

	held.release();

	acquire_desc.timeout_ms = 2000;
	auto latest_result = receiver.acquire_frame(acquire_desc);
	REQUIRE(latest_result.ok());
	auto latest = std::move(latest_result.value());
	REQUIRE(latest.valid());
	REQUIRE(latest.info().frame_index == 6);
	REQUIRE(latest.info().width == 8);
	REQUIRE(latest.info().height == 4);

	auto pixels_result = nozzle::lock_frame_pixels_with_origin(latest, nozzle::texture_origin::top_left);
	REQUIRE(pixels_result.ok());
	auto pixels = pixels_result.value();
	REQUIRE(pixels.data != nullptr);
	REQUIRE(pixels.width == 8);
	REQUIRE(pixels.height == 4);
	const auto *row = static_cast<const uint32_t *>(pixels.data);
	REQUIRE(row[0] == expected_pixel);
	nozzle::unlock_frame_pixels(latest);

	source_8x4->Release();
	source_4x4->Release();
	context->Release();
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
