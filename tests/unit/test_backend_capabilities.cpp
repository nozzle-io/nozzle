#include <catch2/catch_test_macros.hpp>

#include <nozzle/backend_capabilities.hpp>
#include <nozzle/types.hpp>

#include "backend_capabilities_docs.hpp"

#include <fstream>
#include <sstream>
#include <string>

using namespace nozzle;

namespace {

std::string read_file(const char *path) {
    std::ifstream input(path);
    REQUIRE(input.good());
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

std::string extract_generated_block(const std::string &text) {
    const std::string begin = "<!-- NOZZLE_BACKEND_CAPABILITIES_TABLE:BEGIN -->\n";
    const std::string end = "<!-- NOZZLE_BACKEND_CAPABILITIES_TABLE:END -->";
    auto begin_pos = text.find(begin);
    REQUIRE(begin_pos != std::string::npos);
    begin_pos += begin.size();
    auto end_pos = text.find(end, begin_pos);
    REQUIRE(end_pos != std::string::npos);
    return text.substr(begin_pos, end_pos - begin_pos);
}

} // namespace

TEST_CASE("backend capability query rejects unknown backend", "[backend_capabilities]") {
    auto result = get_backend_capabilities(backend_type::unknown);
    REQUIRE(!result.ok());
    CHECK(result.error().code == ErrorCode::UnsupportedBackend);
}

TEST_CASE("backend capability query returns static backend metadata", "[backend_capabilities]") {
    const backend_type backends[] = {
        backend_type::d3d11,
        backend_type::metal,
        backend_type::dma_buf,
        backend_type::opengl,
    };

    for (auto backend : backends) {
        auto result = get_backend_capabilities(backend);
        REQUIRE(result.ok());
        const auto &caps = result.value();
        CHECK(caps.version == backend_capabilities_version);
        CHECK(caps.backend == backend);
        CHECK(caps.native_kind != native_format_kind::unknown);
        CHECK(caps.sharing_mechanisms != 0);
        CHECK(supports_format(caps.requested_format_bits, texture_format::rgba8_unorm));
        CHECK(!supports_format(caps.requested_format_bits, texture_format::unknown));
    }
}

TEST_CASE("backend capability format helper rejects unknown and out-of-range formats", "[backend_capabilities]") {
    uint64_t bits = texture_format_bit(texture_format::rgba8_unorm);
    CHECK(supports_format(bits, texture_format::rgba8_unorm));
    CHECK(!supports_format(bits, texture_format::unknown));
    CHECK(!supports_format(bits, static_cast<texture_format>(64)));
}

TEST_CASE("backend capability keeps RGB semantic separate from Metal and D3D11 storage", "[backend_capabilities]") {
    auto d3d11 = get_backend_capabilities(backend_type::d3d11);
    REQUIRE(d3d11.ok());
    CHECK(supports_format(d3d11->requested_format_bits, texture_format::rgb8_unorm));
    CHECK(!supports_format(d3d11->writable_storage_format_bits, texture_format::rgb8_unorm));
    CHECK(supports_format(d3d11->writable_storage_format_bits, texture_format::rgba8_unorm));

    auto metal = get_backend_capabilities(backend_type::metal);
    REQUIRE(metal.ok());
    CHECK(supports_format(metal->requested_format_bits, texture_format::rgb8_unorm));
    CHECK(!supports_format(metal->writable_storage_format_bits, texture_format::rgb8_unorm));
    CHECK(supports_format(metal->writable_storage_format_bits, texture_format::rgba8_unorm));
}

TEST_CASE("backend capability does not advertise Metal sRGB IOSurface paths rejected by backend", "[backend_capabilities]") {
    auto metal = get_backend_capabilities(backend_type::metal);
    REQUIRE(metal.ok());

    // create_iosurface_texture() rejects rgba8_srgb/bgra8_srgb for IOSurface-backed
    // storage. Writable frames, native publish ring textures, CPU mapping, and the
    // high-level requested semantic path all depend on that storage path.
    CHECK(!supports_format(metal->requested_format_bits, texture_format::rgba8_srgb));
    CHECK(!supports_format(metal->requested_format_bits, texture_format::bgra8_srgb));
    CHECK(!supports_format(metal->writable_storage_format_bits, texture_format::rgba8_srgb));
    CHECK(!supports_format(metal->writable_storage_format_bits, texture_format::bgra8_srgb));
    CHECK(!supports_format(metal->native_publish_format_bits, texture_format::rgba8_srgb));
    CHECK(!supports_format(metal->native_publish_format_bits, texture_format::bgra8_srgb));
    CHECK(!supports_format(metal->direct_publish_format_bits, texture_format::rgba8_srgb));
    CHECK(!supports_format(metal->direct_publish_format_bits, texture_format::bgra8_srgb));
    CHECK(!supports_format(metal->cpu_read_format_bits, texture_format::rgba8_srgb));
    CHECK(!supports_format(metal->cpu_write_format_bits, texture_format::bgra8_srgb));
}

TEST_CASE("backend capability does not advertise OpenGL sRGB publish rejected on macOS IOSurface path", "[backend_capabilities]") {
    auto opengl = get_backend_capabilities(backend_type::opengl);
    REQUIRE(opengl.ok());

    // The macOS publish_gl_texture() path rejects sRGB IOSurface storage. Keep
    // the static contract conservative until a platform-specific non-IOSurface
    // sRGB publish path is tested and documented.
    CHECK(!supports_format(opengl->native_publish_format_bits, texture_format::rgba8_srgb));
    CHECK(!supports_format(opengl->native_publish_format_bits, texture_format::bgra8_srgb));
}

TEST_CASE("backend capability docs matrix matches compiled table", "[backend_capabilities][docs]") {
    auto docs = read_file(NOZZLE_SOURCE_DIR "/docs/backend-capabilities.md");
    CHECK(extract_generated_block(docs) == nozzle::detail::backend_capabilities_markdown_matrix());
}

#if NOZZLE_HAS_D3D11
TEST_CASE("compiled D3D11 capability table has required operation bits", "[backend_capabilities]") {
    auto result = get_backend_capabilities(backend_type::d3d11);
    REQUIRE(result.ok());
    CHECK((result->capability_flags & static_cast<uint32_t>(backend_capability_flags::sender)) != 0);
    CHECK((result->capability_flags & static_cast<uint32_t>(backend_capability_flags::receiver)) != 0);
    CHECK(supports_format(result->writable_storage_format_bits, texture_format::rgba8_unorm));
    CHECK(supports_format(result->native_publish_format_bits, texture_format::rgba8_unorm));
}
#endif

#if NOZZLE_HAS_METAL
TEST_CASE("compiled Metal capability table has required operation bits", "[backend_capabilities]") {
    auto result = get_backend_capabilities(backend_type::metal);
    REQUIRE(result.ok());
    CHECK((result->capability_flags & static_cast<uint32_t>(backend_capability_flags::sender)) != 0);
    CHECK((result->capability_flags & static_cast<uint32_t>(backend_capability_flags::receiver)) != 0);
    CHECK(supports_format(result->writable_storage_format_bits, texture_format::rgba8_unorm));
    CHECK(supports_format(result->direct_publish_format_bits, texture_format::rgba8_unorm));
}
#endif

#if NOZZLE_HAS_DMA_BUF
TEST_CASE("compiled DMA-BUF capability table is conservative about direct float publish", "[backend_capabilities]") {
    auto result = get_backend_capabilities(backend_type::dma_buf);
    REQUIRE(result.ok());
    CHECK((result->capability_flags & static_cast<uint32_t>(backend_capability_flags::sender)) != 0);
    CHECK((result->capability_flags & static_cast<uint32_t>(backend_capability_flags::single_sender_per_process)) != 0);
    CHECK(supports_format(result->direct_publish_format_bits, texture_format::rgba8_unorm));
    CHECK(!supports_format(result->direct_publish_format_bits, texture_format::rgba32_float));
}
#endif
