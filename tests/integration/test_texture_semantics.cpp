#include <catch2/catch_test_macros.hpp>

#include <nozzle/texture.hpp>
#include "backends/backend_dispatch.hpp"

#include <type_traits>

// Native enum values and serialized integers must be translated explicitly at
// the backend boundary, never accepted as nozzle storage or semantic formats.
using texture_factory = decltype(&nozzle::detail::make_texture_from_backend);
static_assert(std::is_same_v<texture_factory,
    nozzle::texture (*)(void *, void *, uint32_t, uint32_t, nozzle::texture_format,
                        uint8_t, const nozzle::native_format_desc *, nozzle::texture_format)>);
static_assert(!std::is_invocable_v<texture_factory, void *, void *, uint32_t, uint32_t,
    uint32_t, uint8_t, const nozzle::native_format_desc *, nozzle::texture_format>);
static_assert(!std::is_invocable_v<texture_factory, void *, void *, uint32_t, uint32_t,
    nozzle::texture_format, uint8_t, const nozzle::native_format_desc *, uint32_t>);
static_assert(std::is_same_v<decltype(&nozzle::detail::backend::create_ring_texture),
    nozzle::Result<nozzle::texture> (*)(void *, uint32_t, uint32_t,
                                       nozzle::texture_format, uint32_t)>);

TEST_CASE("Texture construction: descriptor and resolved semantics agree",
          "[integration][shared_layout][texture_semantics]") {
    struct semantic_case {
        const char *name;
        nozzle::texture_format storage;
        nozzle::texture_format requested;
        nozzle::texture_format expected;
        nozzle::channel_swizzle swizzle;
    };
    const semantic_case cases[]{
        {"implicit BGRA", nozzle::texture_format::bgra8_unorm,
         nozzle::texture_format::unknown, nozzle::texture_format::bgra8_unorm,
         nozzle::channel_swizzle::identity},
        {"explicit RGB in RGBA storage", nozzle::texture_format::rgba8_unorm,
         nozzle::texture_format::rgb8_unorm, nozzle::texture_format::rgb8_unorm,
         nozzle::channel_swizzle::identity},
        {"explicit RGBA in BGRA storage with swizzle", nozzle::texture_format::bgra8_unorm,
         nozzle::texture_format::rgba8_unorm, nozzle::texture_format::rgba8_unorm,
         nozzle::channel_swizzle::swap_rb},
    };
    for (const auto &c : cases) {
        DYNAMIC_SECTION(c.name) {
            nozzle::native_format_desc native{};
            native.backend = nozzle::backend_type::metal;
            native.kind = nozzle::native_format_kind::mtl_pixel_format;
            native.value = c.storage == nozzle::texture_format::bgra8_unorm ? 80 : 70;
            // No resources or backend calls: this deliberately invalid texture
            // tests descriptor construction, not backend support or transport.
            auto texture = nozzle::detail::make_texture_from_backend(
                nullptr, nullptr, 7, 3, c.storage,
                static_cast<uint8_t>(c.swizzle), &native,
                c.requested);
            CHECK_FALSE(texture.valid());
            const auto &desc = texture.desc();
            const auto &resolved = texture.resolved();
            CHECK(desc.width == 7);
            CHECK(desc.height == 3);
            CHECK(desc.format == c.storage);
            CHECK(desc.semantic_format == c.expected);
            CHECK(resolved.semantic_format == c.expected);
            CHECK(desc.semantic_format == resolved.semantic_format);
            CHECK(resolved.storage_format == c.storage);
            CHECK(desc.swizzle == c.swizzle);
            CHECK(resolved.swizzle == c.swizzle);
            CHECK(resolved.native.backend == native.backend);
            CHECK(resolved.native.kind == native.kind);
            CHECK(resolved.native.value == native.value);
            CHECK(resolved.source == nozzle::format_source::native_observed);
            CHECK(resolved.cpu_layout.bytes_per_pixel == 4);
            CHECK(resolved.cpu_layout.channel_count == 4);
            CHECK(resolved.cpu_layout.order == (c.storage == nozzle::texture_format::bgra8_unorm
                ? nozzle::channel_order::bgra : nozzle::channel_order::rgba));
        }
    }
}
