#include <catch2/catch_test_macros.hpp>

#include <nozzle/nozzle.hpp>
#include <nozzle/backends/metal.hpp>
#include "backends/metal/metal_helpers.hpp"

#import <Metal/Metal.h>
#import <IOSurface/IOSurface.h>

#include <mach-o/dyld.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <utility>
#include <vector>

static const uint32_t kTestW = 4;
static const uint32_t kTestH = 4;
static const uint32_t kBPP = 4;
static const uint32_t kRowBytes = kTestW * kBPP;

static void fill_texture(id<MTLTexture> tex, uint8_t value) {
	uint8_t data[kTestH * kRowBytes];
	memset(data, value, sizeof(data));
	MTLRegion region = MTLRegionMake2D(0, 0, kTestW, kTestH);
	[tex replaceRegion:region mipmapLevel:0 withBytes:data bytesPerRow:kRowBytes];
}

static void read_texture(id<MTLTexture> tex, uint8_t *out) {
	MTLRegion region = MTLRegionMake2D(0, 0, kTestW, kTestH);
	[tex getBytes:out bytesPerRow:kRowBytes fromRegion:region mipmapLevel:0];
}

static bool verify_fill(const uint8_t *data, size_t len, uint8_t expected) {
	for (size_t i = 0; i < len; ++i) {
		if (data[i] != expected) return false;
	}
	return true;
}

static id<MTLTexture> create_blit_texture(id<MTLDevice> device) {
	MTLTextureDescriptor *desc =
		[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
		                                                   width:kTestW
		                                                  height:kTestH
		                                               mipmapped:NO];
	desc.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;
	desc.resourceOptions = MTLResourceStorageModeShared;
	return [device newTextureWithDescriptor:desc];
}

static id<MTLTexture> create_iosurface_texture(id<MTLDevice> device, bool globally_lookupable = true) {
	uint32_t bytes_per_row = ((kTestW * kBPP) + 63) & ~63u;
	NSMutableDictionary *props = [@{
		(id)kIOSurfaceWidth:           @(kTestW),
		(id)kIOSurfaceHeight:          @(kTestH),
		(id)kIOSurfacePixelFormat:     @(static_cast<uint32_t>('BGRA')),
		(id)kIOSurfaceBytesPerRow:     @(bytes_per_row),
		(id)kIOSurfaceBytesPerElement: @(kBPP),
	} mutableCopy];
	if (globally_lookupable) {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
		[props setObject:@(YES) forKey:(id)kIOSurfaceIsGlobal];
#pragma clang diagnostic pop
	}
	IOSurfaceRef surface = IOSurfaceCreate((CFDictionaryRef)props);
	[props release];
	if (!surface) return nil;

	MTLTextureDescriptor *desc =
		[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
		                                                   width:kTestW
		                                                  height:kTestH
		                                               mipmapped:NO];
	desc.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;
	desc.resourceOptions = MTLResourceStorageModeShared;

	id<MTLTexture> tex = [device newTextureWithDescriptor:desc iosurface:surface plane:0];
	CFRelease(surface);
	return tex;
}

static IOSurfaceID texture_iosurface_id(id<MTLTexture> tex) {
	REQUIRE(tex != nil);
	REQUIRE([tex iosurface] != nil);
	return IOSurfaceGetID([tex iosurface]);
}

static IOSurfaceID frame_iosurface_id(const nozzle::frame &frame) {
	auto &frame_texture = frame.get_texture();
	REQUIRE(frame_texture.valid());
	void *surface_ptr = nozzle::metal::get_io_surface(frame_texture);
	REQUIRE(surface_ptr != nullptr);
	return IOSurfaceGetID(static_cast<IOSurfaceRef>(surface_ptr));
}

static void require_frame_payload(
	const nozzle::frame &frame,
	id<MTLDevice> device,
	uint8_t expected)
{
	id<MTLTexture> dst = create_blit_texture(device);
	REQUIRE(dst != nil);
	REQUIRE(frame.copy_to_native_texture(
		static_cast<void *>(dst), kTestW, kTestH, nozzle::texture_format::bgra8_unorm).ok());

	uint8_t dst_data[kTestH * kRowBytes];
	read_texture(dst, dst_data);
	REQUIRE(verify_fill(dst_data, sizeof(dst_data), expected));
	[dst release];
}

static nozzle::Result<void> publish_direct_bgra8(
	nozzle::sender &sender,
	id<MTLTexture> texture,
	nozzle::texture_format storage_format = nozzle::texture_format::bgra8_unorm,
	nozzle::texture_format semantic_format = nozzle::texture_format::bgra8_unorm)
{
	nozzle::metal::direct_publish_desc desc{};
	desc.texture = static_cast<void *>(texture);
	desc.width = kTestW;
	desc.height = kTestH;
	desc.storage_format = storage_format;
	desc.semantic_format = semantic_format;
	desc.swizzle = nozzle::channel_swizzle::identity;
	return sender.publish_metal_texture_direct(desc);
}

static nozzle::receiver create_receiver(const char *name) {
	nozzle::receiver_desc rdesc{};
	rdesc.name = name;
	rdesc.application_name = "test";
	auto result = nozzle::receiver::create(rdesc);
	REQUIRE(result.ok());
	return std::move(result.value());
}

static nozzle::sender create_sender(const char *name, id<MTLDevice> device) {
	nozzle::sender_desc sdesc{};
	sdesc.name = name;
	sdesc.application_name = "test";
	sdesc.ring_buffer_size = 2;
	sdesc.native_device = nozzle::native_device_desc{
		nozzle::backend_type::metal, static_cast<void *>(device), nullptr
	};
	auto result = nozzle::sender::create(sdesc);
	REQUIRE(result.ok());
	return std::move(result.value());
}

static std::string current_executable_directory() {
	uint32_t size = 4096;
	std::vector<char> buffer(size);
	if (_NSGetExecutablePath(buffer.data(), &size) != 0) {
		buffer.resize(size);
		if (_NSGetExecutablePath(buffer.data(), &size) != 0) {
			return {};
		}
	}
	std::string path(buffer.data());
	size_t pos = path.find_last_of('/');
	if (pos == std::string::npos) {
		return {};
	}
	return path.substr(0, pos + 1);
}

static std::string make_temp_ready_path(const char *prefix) {
	char path[256]{};
	std::snprintf(path, sizeof(path), "/tmp/%s_%ld_%llu.txt",
		prefix,
		static_cast<long>(getpid()),
		static_cast<unsigned long long>(
			std::chrono::steady_clock::now().time_since_epoch().count()));
	return path;
}

static bool wait_for_file(const std::string &path, uint32_t timeout_ms) {
	auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
	while (std::chrono::steady_clock::now() < deadline) {
		if (access(path.c_str(), F_OK) == 0) {
			return true;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(25));
	}
	return false;
}

static uint64_t read_u64_file(const std::string &path) {
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
	pid_t pid{-1};

	~child_process() {
		if (0 < pid) {
			kill(pid, SIGTERM);
			int status = 0;
			waitpid(pid, &status, 0);
		}
	}

	child_process() = default;
	child_process(const child_process &) = delete;
	child_process &operator=(const child_process &) = delete;
};

static bool start_metal_direct_helper(
	const std::string &sender_name,
	const std::string &ready_file,
	child_process &child)
{
	std::string exe = current_executable_directory() + "nozzle_metal_direct_cross_process_sender";
	pid_t pid = fork();
	if (pid < 0) {
		return false;
	}
	if (pid == 0) {
		execl(exe.c_str(), exe.c_str(), sender_name.c_str(), ready_file.c_str(), nullptr);
		_exit(127);
	}
	child.pid = pid;
	return true;
}


TEST_CASE("Metal lookup uses sender-recorded native format", "[metal][native_publish][native_format]") {
	@autoreleasepool {
		id<MTLDevice> device = MTLCreateSystemDefaultDevice();
		if (device == nil) { SKIP("Metal device is not available on this runner"); }

		id<MTLTexture> src = create_iosurface_texture(device);
		REQUIRE(src != nil);
		REQUIRE([src iosurface] != nil);

		IOSurfaceID source_id = IOSurfaceGetID([src iosurface]);
		auto lookup_result = nozzle::metal::lookup_iosurface_texture(
			static_cast<uint32_t>(source_id),
			kTestW,
			kTestH,
			static_cast<uint32_t>(nozzle::texture_format::rgba8_unorm),
			0,
			static_cast<uint32_t>(nozzle::texture_format::rgba8_unorm),
			static_cast<uint8_t>(nozzle::native_format_kind::mtl_pixel_format),
			static_cast<uint32_t>(MTLPixelFormatBGRA8Unorm));
		REQUIRE(lookup_result.ok());

		const auto &resolved = lookup_result.value().resolved();
		REQUIRE(resolved.native.kind == nozzle::native_format_kind::mtl_pixel_format);
		REQUIRE(resolved.native.value == static_cast<uint32_t>(MTLPixelFormatBGRA8Unorm));
		REQUIRE(resolved.storage_format == nozzle::texture_format::rgba8_unorm);

		[src release];
		[device release];
	}
}

TEST_CASE("Metal blit: receiver gets pixel data from non-IOSurface texture", "[metal][native_publish]") {
	@autoreleasepool {
		id<MTLDevice> device = MTLCreateSystemDefaultDevice();
		if (device == nil) { SKIP("Metal device is not available on this runner"); }

		id<MTLTexture> src = create_blit_texture(device);
		REQUIRE(src != nil);
		fill_texture(src, 0xAB);

		auto sender = create_sender("test_blit_data", device);
		REQUIRE(sender.publish_native_texture(
			static_cast<void *>(src), kTestW, kTestH, nozzle::texture_format::bgra8_unorm).ok());

		auto recv = create_receiver("test_blit_data");
		auto frame_result = recv.acquire_frame();
		REQUIRE(frame_result.ok());

		auto info = frame_result.value().info();
		REQUIRE(info.width == kTestW);
		REQUIRE(info.height == kTestH);
		REQUIRE(info.frame_index > 0);

		id<MTLTexture> dst = create_blit_texture(device);
		REQUIRE(dst != nil);
		REQUIRE(frame_result.value().copy_to_native_texture(
			static_cast<void *>(dst), kTestW, kTestH, nozzle::texture_format::bgra8_unorm).ok());

		uint8_t dst_data[kTestH * kRowBytes];
		read_texture(dst, dst_data);
		REQUIRE(verify_fill(dst_data, sizeof(dst_data), 0xAB));

		[src release];
		[dst release];
		[device release];
	}
}

TEST_CASE("Metal snapshot: IOSurface-backed source is blitted, not shared directly", "[metal][native_publish]") {
	@autoreleasepool {
		id<MTLDevice> device = MTLCreateSystemDefaultDevice();
		if (device == nil) { SKIP("Metal device is not available on this runner"); }

		id<MTLTexture> src = create_iosurface_texture(device);
		REQUIRE(src != nil);
		REQUIRE([src iosurface] != nil);
		fill_texture(src, 0xCD);

		IOSurfaceID source_id = IOSurfaceGetID([src iosurface]);
		REQUIRE(source_id != 0);

		auto sender = create_sender("test_snapshot_iosurface", device);
		REQUIRE(sender.publish_native_texture(
			static_cast<void *>(src), kTestW, kTestH, nozzle::texture_format::bgra8_unorm).ok());

		auto recv = create_receiver("test_snapshot_iosurface");
		auto frame_result = recv.acquire_frame();
		REQUIRE(frame_result.ok());

		auto &frame_texture = frame_result.value().get_texture();
		REQUIRE(frame_texture.valid());

		void *recv_surface_ptr = nozzle::metal::get_io_surface(frame_texture);
		REQUIRE(recv_surface_ptr != nullptr);
		IOSurfaceID recv_id = IOSurfaceGetID(static_cast<IOSurfaceRef>(recv_surface_ptr));

		REQUIRE(recv_id != source_id);

		id<MTLTexture> dst = create_blit_texture(device);
		REQUIRE(dst != nil);
		REQUIRE(frame_result.value().copy_to_native_texture(
			static_cast<void *>(dst), kTestW, kTestH, nozzle::texture_format::bgra8_unorm).ok());

		uint8_t dst_data[kTestH * kRowBytes];
		read_texture(dst, dst_data);
		REQUIRE(verify_fill(dst_data, sizeof(dst_data), 0xCD));

		[src release];
		[dst release];
		[device release];
	}
}

TEST_CASE("Metal snapshot: non-IOSurface source mutation after publish does not affect receiver", "[metal][native_publish]") {
	@autoreleasepool {
		id<MTLDevice> device = MTLCreateSystemDefaultDevice();
		if (device == nil) { SKIP("Metal device is not available on this runner"); }

		id<MTLTexture> src = create_blit_texture(device);
		REQUIRE(src != nil);
		fill_texture(src, 0x42);

		auto sender = create_sender("test_snapshot_mutation", device);
		REQUIRE(sender.publish_native_texture(
			static_cast<void *>(src), kTestW, kTestH, nozzle::texture_format::bgra8_unorm).ok());

		fill_texture(src, 0xEE);

		auto recv = create_receiver("test_snapshot_mutation");
		auto frame_result = recv.acquire_frame();
		REQUIRE(frame_result.ok());

		id<MTLTexture> dst = create_blit_texture(device);
		REQUIRE(dst != nil);
		REQUIRE(frame_result.value().copy_to_native_texture(
			static_cast<void *>(dst), kTestW, kTestH, nozzle::texture_format::bgra8_unorm).ok());

		uint8_t dst_data[kTestH * kRowBytes];
		read_texture(dst, dst_data);
		REQUIRE(verify_fill(dst_data, sizeof(dst_data), 0x42));

		[src release];
		[dst release];
		[device release];
	}
}

TEST_CASE("Metal snapshot: IOSurface-backed source mutation after publish does not affect receiver", "[metal][native_publish]") {
	@autoreleasepool {
		id<MTLDevice> device = MTLCreateSystemDefaultDevice();
		if (device == nil) { SKIP("Metal device is not available on this runner"); }

		id<MTLTexture> src = create_iosurface_texture(device);
		REQUIRE(src != nil);
		REQUIRE([src iosurface] != nil);
		fill_texture(src, 0x55);

		auto sender = create_sender("test_snapshot_iosurface_mutation", device);
		REQUIRE(sender.publish_native_texture(
			static_cast<void *>(src), kTestW, kTestH, nozzle::texture_format::bgra8_unorm).ok());

		fill_texture(src, 0xFF);

		auto recv = create_receiver("test_snapshot_iosurface_mutation");
		auto frame_result = recv.acquire_frame();
		REQUIRE(frame_result.ok());

		id<MTLTexture> dst = create_blit_texture(device);
		REQUIRE(dst != nil);
		REQUIRE(frame_result.value().copy_to_native_texture(
			static_cast<void *>(dst), kTestW, kTestH, nozzle::texture_format::bgra8_unorm).ok());

		uint8_t dst_data[kTestH * kRowBytes];
		read_texture(dst, dst_data);
		REQUIRE(verify_fill(dst_data, sizeof(dst_data), 0x55));

		[src release];
		[dst release];
		[device release];
	}
}

TEST_CASE("Metal snapshot: receiver reads frame after source is released", "[metal][native_publish]") {
	@autoreleasepool {
		id<MTLDevice> device = MTLCreateSystemDefaultDevice();
		if (device == nil) { SKIP("Metal device is not available on this runner"); }

		nozzle::sender sender{};
		nozzle::receiver recv{};
		IOSurfaceID source_id = 0;
		{
			id<MTLTexture> src = create_iosurface_texture(device);
			REQUIRE(src != nil);
			fill_texture(src, 0x77);
			source_id = IOSurfaceGetID([src iosurface]);

			sender = create_sender("test_snapshot_release", device);
			REQUIRE(sender.publish_native_texture(
				static_cast<void *>(src), kTestW, kTestH, nozzle::texture_format::bgra8_unorm).ok());

			recv = create_receiver("test_snapshot_release");
			[src release];
		}

		auto frame_result = recv.acquire_frame();
		REQUIRE(frame_result.ok());

		auto &frame_texture = frame_result.value().get_texture();
		REQUIRE(frame_texture.valid());

		void *recv_surface_ptr = nozzle::metal::get_io_surface(frame_texture);
		REQUIRE(recv_surface_ptr != nullptr);
		IOSurfaceID recv_id = IOSurfaceGetID(static_cast<IOSurfaceRef>(recv_surface_ptr));
		REQUIRE(recv_id != source_id);

		id<MTLTexture> dst = create_blit_texture(device);
		REQUIRE(dst != nil);
		REQUIRE(frame_result.value().copy_to_native_texture(
			static_cast<void *>(dst), kTestW, kTestH, nozzle::texture_format::bgra8_unorm).ok());

		uint8_t dst_data[kTestH * kRowBytes];
		read_texture(dst, dst_data);
		REQUIRE(verify_fill(dst_data, sizeof(dst_data), 0x77));

		[dst release];
		[device release];
	}
}

TEST_CASE("Metal direct: IOSurface-backed texture is shared by identity", "[metal][direct_publish]") {
	@autoreleasepool {
		id<MTLDevice> device = MTLCreateSystemDefaultDevice();
		if (device == nil) { SKIP("Metal device is not available on this runner"); }

		id<MTLTexture> src = create_iosurface_texture(device);
		REQUIRE(src != nil);
		fill_texture(src, 0x31);
		IOSurfaceID source_id = texture_iosurface_id(src);
		REQUIRE(source_id != 0);

		auto sender = create_sender("test_direct_identity", device);
		REQUIRE(publish_direct_bgra8(sender, src).ok());

		auto recv = create_receiver("test_direct_identity");
		auto frame_result = recv.acquire_frame();
		REQUIRE(frame_result.ok());
		auto frame = std::move(frame_result.value());
		REQUIRE(frame_iosurface_id(frame) == source_id);
		require_frame_payload(frame, device, 0x31);
		REQUIRE(frame.get_texture().resolved().native.kind == nozzle::native_format_kind::mtl_pixel_format);
		REQUIRE(frame.get_texture().resolved().native.value == static_cast<uint32_t>(src.pixelFormat));
		REQUIRE(frame.get_texture().desc().format == nozzle::texture_format::bgra8_unorm);
		REQUIRE(frame.get_texture().resolved().semantic_format == nozzle::texture_format::bgra8_unorm);

		[src release];
		[device release];
	}
}

TEST_CASE("Metal direct: non-IOSurface rejection does not publish a new frame", "[metal][direct_publish]") {
	@autoreleasepool {
		id<MTLDevice> device = MTLCreateSystemDefaultDevice();
		if (device == nil) { SKIP("Metal device is not available on this runner"); }

		id<MTLTexture> valid = create_iosurface_texture(device);
		REQUIRE(valid != nil);
		fill_texture(valid, 0x41);
		IOSurfaceID valid_id = texture_iosurface_id(valid);

		auto sender = create_sender("test_direct_reject_non_iosurface", device);
		REQUIRE(publish_direct_bgra8(sender, valid).ok());

		id<MTLTexture> invalid = create_blit_texture(device);
		REQUIRE(invalid != nil);
		fill_texture(invalid, 0x99);
		auto reject_result = publish_direct_bgra8(sender, invalid);
		REQUIRE_FALSE(reject_result.ok());
		REQUIRE((reject_result.error().code == nozzle::ErrorCode::InvalidArgument ||
			reject_result.error().code == nozzle::ErrorCode::UnsupportedFormat));

		auto recv = create_receiver("test_direct_reject_non_iosurface");
		auto frame_result = recv.acquire_frame();
		REQUIRE(frame_result.ok());
		auto frame = std::move(frame_result.value());
		REQUIRE(frame.info().frame_index == 1);
		REQUIRE(frame_iosurface_id(frame) == valid_id);
		require_frame_payload(frame, device, 0x41);

		[valid release];
		[invalid release];
		[device release];
	}
}

TEST_CASE("Metal direct: IOSurface storage mismatch rejection is atomic", "[metal][direct_publish]") {
	@autoreleasepool {
		id<MTLDevice> device = MTLCreateSystemDefaultDevice();
		if (device == nil) { SKIP("Metal device is not available on this runner"); }

		id<MTLTexture> valid = create_iosurface_texture(device);
		REQUIRE(valid != nil);
		fill_texture(valid, 0x52);
		IOSurfaceID valid_id = texture_iosurface_id(valid);

		auto sender = create_sender("test_direct_reject_format_mismatch", device);
		REQUIRE(publish_direct_bgra8(sender, valid).ok());

		id<MTLTexture> mismatch = create_iosurface_texture(device);
		REQUIRE(mismatch != nil);
		fill_texture(mismatch, 0xAA);
		auto reject_result = publish_direct_bgra8(
			sender,
			mismatch,
			nozzle::texture_format::rgba8_unorm,
			nozzle::texture_format::rgba8_unorm);
		REQUIRE_FALSE(reject_result.ok());
		REQUIRE(reject_result.error().code == nozzle::ErrorCode::UnsupportedFormat);

		auto recv = create_receiver("test_direct_reject_format_mismatch");
		auto frame_result = recv.acquire_frame();
		REQUIRE(frame_result.ok());
		auto frame = std::move(frame_result.value());
		REQUIRE(frame.info().frame_index == 1);
		REQUIRE(frame_iosurface_id(frame) == valid_id);
		require_frame_payload(frame, device, 0x52);

		[valid release];
		[mismatch release];
		[device release];
	}
}

TEST_CASE("Metal direct: live-reference mutation is visible after publish", "[metal][direct_publish]") {
	@autoreleasepool {
		id<MTLDevice> device = MTLCreateSystemDefaultDevice();
		if (device == nil) { SKIP("Metal device is not available on this runner"); }

		id<MTLTexture> src = create_iosurface_texture(device);
		REQUIRE(src != nil);
		fill_texture(src, 0x61);

		auto sender = create_sender("test_direct_live_mutation", device);
		REQUIRE(publish_direct_bgra8(sender, src).ok());

		fill_texture(src, 0xE1);

		auto recv = create_receiver("test_direct_live_mutation");
		auto frame_result = recv.acquire_frame();
		REQUIRE(frame_result.ok());
		auto frame = std::move(frame_result.value());
		REQUIRE(frame_iosurface_id(frame) == texture_iosurface_id(src));
		require_frame_payload(frame, device, 0xE1);

		[src release];
		[device release];
	}
}

TEST_CASE("Metal direct: sender retains IOSurface after caller releases texture", "[metal][direct_publish]") {
	@autoreleasepool {
		id<MTLDevice> device = MTLCreateSystemDefaultDevice();
		if (device == nil) { SKIP("Metal device is not available on this runner"); }

		auto sender = create_sender("test_direct_caller_release", device);
		IOSurfaceID source_id = 0;
		{
			id<MTLTexture> src = create_iosurface_texture(device);
			REQUIRE(src != nil);
			fill_texture(src, 0x72);
			source_id = texture_iosurface_id(src);
			REQUIRE(publish_direct_bgra8(sender, src).ok());
			[src release];
		}

		auto recv = create_receiver("test_direct_caller_release");
		auto frame_result = recv.acquire_frame();
		REQUIRE(frame_result.ok());
		auto frame = std::move(frame_result.value());
		REQUIRE(frame_iosurface_id(frame) == source_id);
		require_frame_payload(frame, device, 0x72);

		[device release];
	}
}

TEST_CASE("Metal direct: receiver-retained frame survives sender slot replacement", "[metal][direct_publish]") {
	@autoreleasepool {
		id<MTLDevice> device = MTLCreateSystemDefaultDevice();
		if (device == nil) { SKIP("Metal device is not available on this runner"); }

		nozzle::sender_desc sdesc{};
		sdesc.name = "test_direct_slot_replacement";
		sdesc.application_name = "test";
		sdesc.ring_buffer_size = 2;
		sdesc.native_device = nozzle::native_device_desc{
			nozzle::backend_type::metal, static_cast<void *>(device), nullptr
		};
		auto sender_result = nozzle::sender::create(sdesc);
		REQUIRE(sender_result.ok());
		auto sender = std::move(sender_result.value());

		id<MTLTexture> texture_a = create_iosurface_texture(device);
		REQUIRE(texture_a != nil);
		fill_texture(texture_a, 0x81);
		IOSurfaceID id_a = texture_iosurface_id(texture_a);
		REQUIRE(publish_direct_bgra8(sender, texture_a).ok());

		auto recv = create_receiver("test_direct_slot_replacement");
		auto frame_a_result = recv.acquire_frame();
		REQUIRE(frame_a_result.ok());
		auto frame_a = std::move(frame_a_result.value());
		REQUIRE(frame_iosurface_id(frame_a) == id_a);

		IOSurfaceID latest_id = 0;
		uint8_t latest_value = 0;
		for (uint32_t publish_index = 0; publish_index < 3; ++publish_index) {
			id<MTLTexture> tex = create_iosurface_texture(device);
			REQUIRE(tex != nil);
			latest_value = static_cast<uint8_t>(0x90 + publish_index);
			fill_texture(tex, latest_value);
			latest_id = texture_iosurface_id(tex);
			REQUIRE(publish_direct_bgra8(sender, tex).ok());
			[tex release];
		}

		REQUIRE(frame_iosurface_id(frame_a) == id_a);
		require_frame_payload(frame_a, device, 0x81);

		auto latest_result = recv.acquire_frame();
		REQUIRE(latest_result.ok());
		auto latest = std::move(latest_result.value());
		REQUIRE(frame_iosurface_id(latest) == latest_id);
		REQUIRE(latest_id != id_a);
		require_frame_payload(latest, device, latest_value);

		[texture_a release];
		[device release];
	}
}

TEST_CASE("Metal direct: public receiver acquires direct IOSurface from another process", "[metal][direct_publish][cross_process]") {
	@autoreleasepool {
		id<MTLDevice> device = MTLCreateSystemDefaultDevice();
		if (device == nil) { SKIP("Metal device is not available on this runner"); }

		std::string ready_file = make_temp_ready_path("nozzle_metal_direct");
		REQUIRE_FALSE(ready_file.empty());
		unlink(ready_file.c_str());

		char sender_name_buffer[96]{};
		std::snprintf(sender_name_buffer, sizeof(sender_name_buffer), "metal_direct_%ld_%llu",
			static_cast<long>(getpid()),
			static_cast<unsigned long long>(
				std::chrono::steady_clock::now().time_since_epoch().count()));
		std::string sender_name(sender_name_buffer);

		child_process child;
		REQUIRE(start_metal_direct_helper(sender_name, ready_file, child));
		REQUIRE(wait_for_file(ready_file, 10000));
		uint64_t source_id = read_u64_file(ready_file);
		REQUIRE(source_id != 0);

		nozzle::receiver_desc receiver_desc{};
		receiver_desc.name = sender_name;
		receiver_desc.application_name = "metal_direct_cross_process_receiver";
		auto receiver_result = nozzle::receiver::create(receiver_desc);
		REQUIRE(receiver_result.ok());
		auto receiver = std::move(receiver_result.value());

		nozzle::acquire_desc acquire_desc{};
		acquire_desc.timeout_ms = 1000;
		auto frame_result = receiver.acquire_frame(acquire_desc);
		REQUIRE(frame_result.ok());
		auto frame = std::move(frame_result.value());
		REQUIRE(frame_iosurface_id(frame) == static_cast<IOSurfaceID>(source_id));
		require_frame_payload(frame, device, 0xC3);

		unlink(ready_file.c_str());
		[device release];
	}
}

TEST_CASE("Metal ring rotation: receiver gets latest frame with correct payload", "[metal][native_publish]") {
	@autoreleasepool {
		id<MTLDevice> device = MTLCreateSystemDefaultDevice();
		if (device == nil) { SKIP("Metal device is not available on this runner"); }

		auto sender = create_sender("test_rotation", device);
		auto recv = create_receiver("test_rotation");

		for (int i = 0; i < 6; ++i) {
			@autoreleasepool {
				id<MTLTexture> src = create_blit_texture(device);
				REQUIRE(src != nil);
				fill_texture(src, static_cast<uint8_t>(0xA0 + i));

				REQUIRE(sender.publish_native_texture(
					static_cast<void *>(src), kTestW, kTestH, nozzle::texture_format::bgra8_unorm).ok());

				[src release];
			}
		}

		auto frame_result = recv.acquire_frame();
		REQUIRE(frame_result.ok());

		auto info = frame_result.value().info();
		REQUIRE(info.width == kTestW);
		REQUIRE(info.height == kTestH);
		REQUIRE(info.frame_index >= 6);

		id<MTLTexture> dst = create_blit_texture(device);
		REQUIRE(dst != nil);
		REQUIRE(frame_result.value().copy_to_native_texture(
			static_cast<void *>(dst), kTestW, kTestH, nozzle::texture_format::bgra8_unorm).ok());

		uint8_t dst_data[kTestH * kRowBytes];
		read_texture(dst, dst_data);
		REQUIRE(verify_fill(dst_data, sizeof(dst_data), 0xA5));

		[dst release];
		[device release];
	}
}

TEST_CASE("Metal native metadata: publish reports actual MTLPixelFormatBGRA8Unorm", "[metal][native_publish]") {
	@autoreleasepool {
		id<MTLDevice> device = MTLCreateSystemDefaultDevice();
		if (device == nil) { SKIP("Metal device is not available on this runner"); }

		id<MTLTexture> src = create_blit_texture(device);
		REQUIRE(src != nil);
		fill_texture(src, 0xCC);

		auto sender = create_sender("test_native_meta", device);
		REQUIRE(sender.publish_native_texture(
			static_cast<void *>(src), kTestW, kTestH, nozzle::texture_format::bgra8_unorm).ok());

		auto recv = create_receiver("test_native_meta");
		auto frame_result = recv.acquire_frame();
		REQUIRE(frame_result.ok());

		auto &frame = frame_result.value();
		auto &resolved = frame.get_texture().resolved();

		REQUIRE(resolved.native.backend == nozzle::backend_type::metal);
		REQUIRE(resolved.native.kind == nozzle::native_format_kind::mtl_pixel_format);
		REQUIRE(resolved.native.value == static_cast<uint32_t>(MTLPixelFormatBGRA8Unorm));
		REQUIRE(resolved.source == nozzle::format_source::native_observed);

		auto ci = recv.connected_info();
		REQUIRE(ci.native_format_kind == static_cast<uint8_t>(nozzle::native_format_kind::mtl_pixel_format));
		REQUIRE(ci.native_format_value == static_cast<uint32_t>(MTLPixelFormatBGRA8Unorm));

		[src release];
		[device release];
	}
}

TEST_CASE("Metal native metadata: receiver frame texture matches actual MTLPixelFormat", "[metal][native_publish]") {
	@autoreleasepool {
		id<MTLDevice> device = MTLCreateSystemDefaultDevice();
		if (device == nil) { SKIP("Metal device is not available on this runner"); }

		auto sender = create_sender("test_native_recv", device);

		// rgba8_unorm falls back to bgra8_unorm for IOSurface (Apple Silicon limitation)
		auto wf = sender.acquire_writable_frame({
			kTestW, kTestH, nozzle::texture_format::rgba8_unorm
		});
		REQUIRE(wf.ok());

		// verify ring texture itself reports actual BGRA8, not the requested RGBA8
		auto &ring_resolved = wf.value().get_texture().resolved();
		REQUIRE(ring_resolved.native.backend == nozzle::backend_type::metal);
		REQUIRE(ring_resolved.native.kind == nozzle::native_format_kind::mtl_pixel_format);
		REQUIRE(ring_resolved.native.value == static_cast<uint32_t>(MTLPixelFormatBGRA8Unorm));

		sender.commit_frame(wf.value());

		auto recv = create_receiver("test_native_recv");
		auto frame_result = recv.acquire_frame();
		REQUIRE(frame_result.ok());

		auto &resolved = frame_result.value().get_texture().resolved();
		REQUIRE(resolved.native.backend == nozzle::backend_type::metal);
		REQUIRE(resolved.native.kind == nozzle::native_format_kind::mtl_pixel_format);
		REQUIRE(resolved.native.value == static_cast<uint32_t>(MTLPixelFormatBGRA8Unorm));

		[device release];
	}
}

TEST_CASE("Metal device validation: same-device texture passes validation", "[metal][native_publish]") {
	@autoreleasepool {
		id<MTLDevice> device = MTLCreateSystemDefaultDevice();
		if (device == nil) { SKIP("Metal device is not available on this runner"); }

		id<MTLTexture> src = create_blit_texture(device);
		REQUIRE(src != nil);
		fill_texture(src, 0xBB);

		auto sender = create_sender("test_device_match", device);
		REQUIRE(sender.publish_native_texture(
			static_cast<void *>(src), kTestW, kTestH, nozzle::texture_format::bgra8_unorm).ok());

		auto recv = create_receiver("test_device_match");
		auto frame_result = recv.acquire_frame();
		REQUIRE(frame_result.ok());

		id<MTLTexture> dst = create_blit_texture(device);
		REQUIRE(dst != nil);
		REQUIRE(frame_result.value().copy_to_native_texture(
			static_cast<void *>(dst), kTestW, kTestH, nozzle::texture_format::bgra8_unorm).ok());

		uint8_t dst_data[kTestH * kRowBytes];
		read_texture(dst, dst_data);
		REQUIRE(verify_fill(dst_data, sizeof(dst_data), 0xBB));

		[src release];
		[dst release];
		[device release];
	}
}

TEST_CASE("Metal native metadata: wrap_texture reports actual MTLPixelFormat from existing texture", "[metal][native_publish]") {
	@autoreleasepool {
		id<MTLDevice> device = MTLCreateSystemDefaultDevice();
		if (device == nil) { SKIP("Metal device is not available on this runner"); }

		id<MTLTexture> tex = create_blit_texture(device);
		REQUIRE(tex != nil);

		nozzle::metal::texture_wrap_desc wrap_desc{};
		wrap_desc.texture = static_cast<void *>(tex);
		wrap_desc.format = static_cast<uint32_t>(nozzle::texture_format::bgra8_unorm);
		wrap_desc.width = kTestW;
		wrap_desc.height = kTestH;

		auto wrap_result = nozzle::metal::wrap_texture(wrap_desc);
		REQUIRE(wrap_result.ok());

		auto &resolved = wrap_result.value().resolved();
		REQUIRE(resolved.native.backend == nozzle::backend_type::metal);
		REQUIRE(resolved.native.kind == nozzle::native_format_kind::mtl_pixel_format);
		REQUIRE(resolved.native.value == static_cast<uint32_t>(tex.pixelFormat));
		REQUIRE(resolved.native.value == static_cast<uint32_t>(MTLPixelFormatBGRA8Unorm));
		REQUIRE(resolved.source == nozzle::format_source::native_observed);

		[tex release];
		[device release];
	}
}
