#include <catch2/catch_test_macros.hpp>

#include <nozzle/sender.hpp>
#include <nozzle/types.hpp>

#if NOZZLE_HAS_METAL
static constexpr nozzle::backend_type actual_backend = nozzle::backend_type::metal;
static constexpr nozzle::backend_type mismatch_backend = nozzle::backend_type::d3d11;
#elif NOZZLE_HAS_D3D11
static constexpr nozzle::backend_type actual_backend = nozzle::backend_type::d3d11;
static constexpr nozzle::backend_type mismatch_backend = nozzle::backend_type::metal;
#elif NOZZLE_HAS_DMA_BUF
static constexpr nozzle::backend_type actual_backend = nozzle::backend_type::dma_buf;
static constexpr nozzle::backend_type mismatch_backend = nozzle::backend_type::d3d11;
#else
static constexpr nozzle::backend_type actual_backend = nozzle::backend_type::unknown;
static constexpr nozzle::backend_type mismatch_backend = nozzle::backend_type::metal;
#endif

TEST_CASE("C++ sender_desc: unknown fallback_flags rejected before device acquisition", "[cpp_api]") {
	nozzle::sender_desc desc{};
	desc.name = "test_invalid_flags";
	desc.fallback_flags = 0x80000000u | nozzle::fallback_allow_storage_compatible;

	auto r = nozzle::sender::create(desc);
	REQUIRE_FALSE(r.ok());
	REQUIRE(r.error().code == nozzle::ErrorCode::InvalidArgument);
}

TEST_CASE("C++ sender_desc: only unknown bits rejected", "[cpp_api]") {
	nozzle::sender_desc desc{};
	desc.name = "test_only_unknown";
	desc.fallback_flags = 0xFF000000u;

	auto r = nozzle::sender::create(desc);
	REQUIRE_FALSE(r.ok());
	REQUIRE(r.error().code == nozzle::ErrorCode::InvalidArgument);
}

TEST_CASE("C++ sender_desc: valid fallback_flags passes descriptor validation", "[cpp_api]") {
	nozzle::sender_desc desc{};
	desc.name = "test_valid_flags";
	desc.fallback_flags = nozzle::fallback_safe_defaults;

	auto r = nozzle::sender::create(desc);
	if (!r.ok()) {
		REQUIRE(r.error().code != nozzle::ErrorCode::InvalidArgument);
	}
}

TEST_CASE("C++ sender: native_device() returns default device when none injected", "[native_device]") {
	nozzle::sender_desc desc{};
	desc.name = "test_native_default";
	desc.native_device = {};

	auto r = nozzle::sender::create(desc);
	if (!r.ok()) { return; }

	auto nd = r.value().native_device();
	REQUIRE(nd.device != nullptr);
	REQUIRE(nd.backend == actual_backend);
}

TEST_CASE("C++ sender: backend mismatch rejected", "[native_device]") {
	nozzle::sender_desc desc{};
	desc.name = "test_backend_mismatch";
	desc.native_device.backend = mismatch_backend;
	desc.native_device.device = reinterpret_cast<void *>(0x1);
	desc.native_device.context = nullptr;

	auto r = nozzle::sender::create(desc);
	REQUIRE_FALSE(r.ok());
	REQUIRE(r.error().code == nozzle::ErrorCode::InvalidArgument);
}

TEST_CASE("C++ sender: unknown backend with non-null device rejected", "[native_device]") {
	nozzle::sender_desc desc{};
	desc.name = "test_unknown_backend";
	desc.native_device.backend = nozzle::backend_type::unknown;
	desc.native_device.device = reinterpret_cast<void *>(0x1);
	desc.native_device.context = nullptr;

	auto r = nozzle::sender::create(desc);
	REQUIRE_FALSE(r.ok());
	REQUIRE(r.error().code == nozzle::ErrorCode::InvalidArgument);
}

TEST_CASE("C++ sender: matching backend with non-null device accepted", "[native_device]") {
	nozzle::sender_desc desc{};
	desc.name = "test_matching_backend";
	desc.native_device.backend = actual_backend;
	desc.native_device.device = reinterpret_cast<void *>(0x1);
	desc.native_device.context = nullptr;

	auto r = nozzle::sender::create(desc);
	if (!r.ok()) { return; }

	auto nd = r.value().native_device();
	REQUIRE(nd.device == reinterpret_cast<void *>(0x1));
}
