// nozzle - test_dmabuf_publish.cpp - C API DMA-BUF publish validation tests

#include <catch2/catch_test_macros.hpp>

#include <nozzle/nozzle_c.h>

namespace {

NozzleDmaBufPublishDesc valid_dmabuf_desc() {
    NozzleDmaBufPublishDesc desc{};
    desc.dmabuf_fd = 0;
    desc.width = 16;
    desc.height = 16;
    desc.drm_fourcc = 1;
    desc.modifier = 0;
    desc.plane_count = 1;
    desc.planes[0].stride = 64;
    desc.planes[0].offset = 0;
    desc.storage_format = NOZZLE_FORMAT_RGBA8_UNORM;
    desc.semantic_format = NOZZLE_FORMAT_RGBA8_UNORM;
    desc.swizzle = NOZZLE_CHANNEL_SWIZZLE_IDENTITY;
    return desc;
}

} // anonymous namespace

TEST_CASE("C API: publish_dmabuf validates null arguments", "[c_api][dmabuf]") {
    NozzleDmaBufPublishDesc desc = valid_dmabuf_desc();

    CHECK(nozzle_sender_publish_dmabuf(nullptr, nullptr) == NOZZLE_ERROR_INVALID_ARGUMENT);
    CHECK(nozzle_sender_publish_dmabuf(nullptr, &desc) == NOZZLE_ERROR_INVALID_ARGUMENT);
}

TEST_CASE("C API: publish_dmabuf validates descriptor before backend use", "[c_api][dmabuf]") {
    NozzleDmaBufPublishDesc desc = valid_dmabuf_desc();

    SECTION("invalid fd") {
        desc.dmabuf_fd = -1;
        CHECK(nozzle_sender_publish_dmabuf(nullptr, &desc) == NOZZLE_ERROR_INVALID_ARGUMENT);
    }

    SECTION("zero width") {
        desc.width = 0;
        CHECK(nozzle_sender_publish_dmabuf(nullptr, &desc) == NOZZLE_ERROR_INVALID_ARGUMENT);
    }

    SECTION("zero height") {
        desc.height = 0;
        CHECK(nozzle_sender_publish_dmabuf(nullptr, &desc) == NOZZLE_ERROR_INVALID_ARGUMENT);
    }

    SECTION("missing fourcc") {
        desc.drm_fourcc = 0;
        CHECK(nozzle_sender_publish_dmabuf(nullptr, &desc) == NOZZLE_ERROR_INVALID_ARGUMENT);
    }

    SECTION("missing stride") {
        desc.planes[0].stride = 0;
        CHECK(nozzle_sender_publish_dmabuf(nullptr, &desc) == NOZZLE_ERROR_INVALID_ARGUMENT);
    }

    SECTION("zero planes") {
        desc.plane_count = 0;
        CHECK(nozzle_sender_publish_dmabuf(nullptr, &desc) == NOZZLE_ERROR_UNSUPPORTED_FORMAT);
    }

    SECTION("multi-plane rejected in v1") {
        desc.plane_count = 2;
        desc.planes[1].stride = 32;
        CHECK(nozzle_sender_publish_dmabuf(nullptr, &desc) == NOZZLE_ERROR_UNSUPPORTED_FORMAT);
    }

    SECTION("unknown storage format") {
        desc.storage_format = NOZZLE_FORMAT_UNKNOWN;
        CHECK(nozzle_sender_publish_dmabuf(nullptr, &desc) == NOZZLE_ERROR_UNSUPPORTED_FORMAT);
    }

    SECTION("unknown semantic format") {
        desc.semantic_format = NOZZLE_FORMAT_UNKNOWN;
        CHECK(nozzle_sender_publish_dmabuf(nullptr, &desc) == NOZZLE_ERROR_UNSUPPORTED_FORMAT);
    }
}
