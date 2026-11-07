#include <catch2/catch_test_macros.hpp>

#include "ipc.hpp"
#include "registry.hpp"
#include "shared_state.hpp"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {
namespace ipc = nozzle::detail::ipc;
namespace registry = nozzle::detail::registry;
namespace nd = nozzle::detail;

struct state_mapping {
    std::string name;
    ipc::shm_handle handle{};
    nd::SenderSharedState *state{nullptr};

    state_mapping() {
        char buffer[32]{};
        uint64_t nonce = 0;
        ipc::random_bytes(&nonce, sizeof(nonce));
        std::snprintf(buffer, sizeof(buffer), "/nl_%08x_%016llx",
            static_cast<unsigned>(ipc::current_pid()),
            static_cast<unsigned long long>(nonce));
        name = buffer;
    }

    ~state_mapping() {
        if (state) ipc::shm_unmap(state, sizeof(*state));
        ipc::shm_close(handle);
        ipc::shm_unlink(name.c_str());
    }

    void create() {
        auto created = ipc::shm_create(name.c_str(), sizeof(nd::SenderSharedState));
        REQUIRE(created.ok());
        handle = created.value();
        auto mapped = ipc::shm_map(handle, sizeof(nd::SenderSharedState), false);
        REQUIRE(mapped.ok());
        state = static_cast<nd::SenderSharedState *>(mapped.value());
        *state = nd::SenderSharedState{};
        state->magic = nd::kSenderMagic;
        state->version = nd::kSharedMemVersion;
        state->ring_size = 2;
        std::memcpy(state->name, "shared_layout_control", sizeof("shared_layout_control"));
        std::memcpy(state->application_name, "layout_tests", sizeof("layout_tests"));
        std::memcpy(state->uuid, "01234567-89ab-cdef-0123-456789abcdef",
            sizeof("01234567-89ab-cdef-0123-456789abcdef"));
    }
};
} // namespace

TEST_CASE("Shared layout: existing sender mapping retains its bytes", "[shared_layout][registry]") {
    state_mapping mapping;
    mapping.create();
    mapping.state->width = 123;
    std::vector<unsigned char> before(sizeof(*mapping.state));
    std::memcpy(before.data(), mapping.state, before.size());

    auto existing = ipc::shm_create(mapping.name.c_str(), sizeof(*mapping.state));
    REQUIRE(existing.ok());
    ipc::shm_close(existing.value());
    CHECK(std::memcmp(before.data(), mapping.state, before.size()) == 0);

    auto opened = registry::open_sender_state(mapping.name.c_str());
    REQUIRE(opened.ok());
    const bool intact = opened.value().state->width == 123
        && std::strcmp(opened.value().state->name, "shared_layout_control") == 0;
    registry::close_sender_state(opened.value());
    CHECK(intact);
}

TEST_CASE("Shared layout: mapped sender headers and strings are validated", "[shared_layout][registry]") {
    state_mapping mapping;
    mapping.create();
    bool version_mismatch = false;
    SECTION("sender version 2 is not directory compatibility") {
        mapping.state->version = 2;
        version_mismatch = true;
    }
    SECTION("sender version 5 is not directory compatibility") {
        mapping.state->version = 5;
        version_mismatch = true;
    }
    SECTION("sender version 6 lacks lifecycle") {
        mapping.state->version = 6;
        version_mismatch = true;
    }
    SECTION("future sender version") {
        mapping.state->version = nd::kSharedMemVersion + 1;
        version_mismatch = true;
    }
    SECTION("invalid lifecycle") { mapping.state->lifecycle_state = nd::sender_state_closed + 1; }
    SECTION("foreign magic") { mapping.state->magic = nd::kSenderMagic ^ 1u; }
    SECTION("zero ring") { mapping.state->ring_size = 0; }
    SECTION("oversized ring") { mapping.state->ring_size = nd::kMaxRingSlots + 1; }
    SECTION("unterminated name") {
        std::memset(mapping.state->name, 'n', sizeof(mapping.state->name));
    }
    SECTION("unterminated application") {
        std::memset(mapping.state->application_name, 'a', sizeof(mapping.state->application_name));
    }
    SECTION("unterminated uuid") {
        std::memset(mapping.state->uuid, 'u', sizeof(mapping.state->uuid));
    }

    auto opened = registry::open_sender_state(mapping.name.c_str());
    const bool accepted = opened.ok();
    if (accepted) registry::close_sender_state(opened.value());
    CHECK_FALSE(accepted);
    if (!accepted && version_mismatch) {
        CHECK(opened.error().code == nozzle::ErrorCode::BackendError);
        CHECK(opened.error().message.find("version") != std::string::npos);
    }
}

TEST_CASE("Shared layout: unpublished sender state is not ready", "[shared_layout][registry]") {
    state_mapping mapping;
    mapping.create();
    SECTION("zeroed backing") { std::memset(mapping.state, 0, sizeof(*mapping.state)); }
    SECTION("magic not yet published") { mapping.state->magic = 0; }
    auto opened = registry::open_sender_state(mapping.name.c_str());
    const bool accepted = opened.ok();
    if (accepted) registry::close_sender_state(opened.value());
    CHECK_FALSE(accepted);
    if (!accepted) CHECK(opened.error().code == nozzle::ErrorCode::SenderNotFound);
}
