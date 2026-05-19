#include <nozzle/nozzle.hpp>

#include "ipc.hpp"
#include "registry.hpp"
#include "shared_state.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace registry = nozzle::detail::registry;
namespace ipc = nozzle::detail::ipc;

struct writable_sender_state {
    ipc::shm_handle handle{};
    void *mapped{nullptr};

    ~writable_sender_state() {
        if (mapped) {
            ipc::shm_unmap(mapped, sizeof(nozzle::detail::SenderSharedState));
        }
        ipc::shm_close(handle);
    }

    writable_sender_state() = default;
    writable_sender_state(const writable_sender_state &) = delete;
    writable_sender_state &operator=(const writable_sender_state &) = delete;
    writable_sender_state(writable_sender_state &&other) noexcept
        : handle{other.handle}
        , mapped{other.mapped}
    {
        other.handle = {};
        other.mapped = nullptr;
    }
    writable_sender_state &operator=(writable_sender_state &&other) noexcept {
        if (this != &other) {
            if (mapped) {
                ipc::shm_unmap(mapped, sizeof(nozzle::detail::SenderSharedState));
            }
            ipc::shm_close(handle);
            handle = other.handle;
            mapped = other.mapped;
            other.handle = {};
            other.mapped = nullptr;
        }
        return *this;
    }
};

static nozzle::Result<writable_sender_state> open_writable_sender_state(const std::string &sender_id) {
    if (sender_id.size() < 8) {
        return nozzle::Error{nozzle::ErrorCode::InvalidArgument, "sender id is too short"};
    }

    char shm_name[96]{};
    std::snprintf(shm_name, sizeof(shm_name), "%s%.8s",
        nozzle::detail::kSenderShmPrefix, sender_id.c_str());

    writable_sender_state view{};
    auto shm_result = ipc::shm_open_rw(shm_name);
    if (!shm_result.ok()) {
        return shm_result.error();
    }
    view.handle = std::move(shm_result.value());

    auto map_result = ipc::shm_map(
        view.handle, sizeof(nozzle::detail::SenderSharedState), false);
    if (!map_result.ok()) {
        ipc::shm_close(view.handle);
        view.handle = {};
        return map_result.error();
    }
    view.mapped = map_result.value();

    auto *state = static_cast<nozzle::detail::SenderSharedState *>(view.mapped);
    if (state->magic != nozzle::detail::kSenderMagic) {
        return nozzle::Error{nozzle::ErrorCode::BackendError, "sender state has invalid magic"};
    }

    return view;
}

static void clear_all_registered_senders() {
    auto senders = nozzle::enumerate_senders();
    for (const auto &s : senders) {
        registry::unregister_sender(s.id.c_str());
    }
}

static bool is_backend_unavailable(const nozzle::Error &error) {
    return error.code == nozzle::ErrorCode::UnsupportedBackend
        || error.code == nozzle::ErrorCode::ResourceCreationFailed
        || error.code == nozzle::ErrorCode::BackendError;
}

static bool contains_sender_named(
    const std::vector<nozzle::sender_info> &senders,
    const std::string &name)
{
    return std::any_of(senders.begin(), senders.end(),
        [&](const nozzle::sender_info &info) {
            return info.name == name;
        });
}

static nozzle::sender_info find_sender_named(
    const std::vector<nozzle::sender_info> &senders,
    const std::string &name)
{
    for (const auto &s : senders) {
        if (s.name == name) {
            return s;
        }
    }
    return {};
}

TEST_CASE("Integration: sender registers and receiver discovers", "[integration]") {
    clear_all_registered_senders();

    nozzle::sender_desc desc{};
    desc.name = "integ_discover_test";
    desc.application_name = "integ_app";

    auto sender_result = nozzle::sender::create(desc);
    if (!sender_result.ok() && is_backend_unavailable(sender_result.error())) {
        SKIP("backend device is not available on this runner");
    }
    REQUIRE(sender_result.ok());
    auto snd = std::move(sender_result.value());

    SECTION("sender appears in enumeration") {
        auto senders = nozzle::enumerate_senders();
        REQUIRE_FALSE(senders.empty());
        REQUIRE(contains_sender_named(senders, "integ_discover_test"));

        auto info = find_sender_named(senders, "integ_discover_test");
        REQUIRE(info.application_name == "integ_app");
#if NOZZLE_HAS_METAL
        REQUIRE(info.backend == nozzle::backend_type::metal);
#endif
    }

    SECTION("sender info matches via public API") {
        auto info = snd.info();
        REQUIRE(info.name == "integ_discover_test");
        REQUIRE(info.application_name == "integ_app");
    }
}

TEST_CASE("Integration: receiver connects to sender", "[integration]") {
    clear_all_registered_senders();

    nozzle::sender_desc desc{};
    desc.name = "integ_connect_test";
    desc.application_name = "integ_app";

    auto sender_result = nozzle::sender::create(desc);
    if (!sender_result.ok() && is_backend_unavailable(sender_result.error())) {
        SKIP("backend device is not available on this runner");
    }
    REQUIRE(sender_result.ok());
    auto snd = std::move(sender_result.value());

    nozzle::receiver_desc rdesc{};
    rdesc.name = "integ_connect_test";
    rdesc.application_name = "recv_app";

    auto recv_result = nozzle::receiver::create(rdesc);
    REQUIRE(recv_result.ok());
    auto recv = std::move(recv_result.value());

    REQUIRE(recv.is_connected());
    REQUIRE(recv.valid());

    auto conn_info = recv.connected_info();
    REQUIRE(conn_info.name == "integ_connect_test");
    REQUIRE(conn_info.application_name == "integ_app");
}

TEST_CASE("Integration: receiver gets no-frame before sender publishes", "[integration]") {
    clear_all_registered_senders();

    nozzle::sender_desc desc{};
    desc.name = "integ_noframe_test";
    desc.application_name = "integ_app";

    auto sender_result = nozzle::sender::create(desc);
    if (!sender_result.ok() && is_backend_unavailable(sender_result.error())) {
        SKIP("backend device is not available on this runner");
    }
    REQUIRE(sender_result.ok());
    auto snd = std::move(sender_result.value());

    nozzle::receiver_desc rdesc{};
    rdesc.name = "integ_noframe_test";

    auto recv_result = nozzle::receiver::create(rdesc);
    REQUIRE(recv_result.ok());
    auto recv = std::move(recv_result.value());

    auto frame_result = recv.acquire_frame();
    REQUIRE(!frame_result.ok());
    REQUIRE(frame_result.error().code == nozzle::ErrorCode::Timeout);
}

TEST_CASE("Integration: sender publish and receiver acquire", "[integration]") {
    clear_all_registered_senders();

    nozzle::sender_desc desc{};
    desc.name = "integ_publish_test";
    desc.application_name = "integ_app";

    auto sender_result = nozzle::sender::create(desc);
    if (!sender_result.ok() && is_backend_unavailable(sender_result.error())) {
        SKIP("backend device is not available on this runner");
    }
    REQUIRE(sender_result.ok());
    auto snd = std::move(sender_result.value());

    nozzle::receiver_desc rdesc{};
    rdesc.name = "integ_publish_test";

    auto recv_result = nozzle::receiver::create(rdesc);
    REQUIRE(recv_result.ok());
    auto recv = std::move(recv_result.value());

    SECTION("acquire_writable_frame and commit") {
        nozzle::texture_desc tdesc{};
        tdesc.width = 64;
        tdesc.height = 64;
        tdesc.format = nozzle::texture_format::rgba8_unorm;

        auto wf_result = snd.acquire_writable_frame(tdesc);
        REQUIRE(wf_result.ok());
        auto wf = std::move(wf_result.value());
        REQUIRE(wf.valid());

        auto commit_result = snd.commit_frame(wf);
        REQUIRE(commit_result.ok());
        REQUIRE(!wf.valid());
    }

    SECTION("receiver acquires published frame") {
        nozzle::texture_desc tdesc{};
        tdesc.width = 64;
        tdesc.height = 64;
        tdesc.format = nozzle::texture_format::rgba8_unorm;

        auto wf_result = snd.acquire_writable_frame(tdesc);
        REQUIRE(wf_result.ok());

        auto commit_result = snd.commit_frame(wf_result.value());
        REQUIRE(commit_result.ok());

        nozzle::acquire_desc adesc{};
        adesc.timeout_ms = 500;

        auto frame_result = recv.acquire_frame(adesc);
        REQUIRE(frame_result.ok());
        auto f = std::move(frame_result.value());
        REQUIRE(f.valid());
        REQUIRE(f.info().width == 64);
        REQUIRE(f.info().height == 64);
        REQUIRE(f.info().frame_index > 0);
        REQUIRE(f.info().transfer_mode_val == nozzle::transfer_mode::zero_copy_shared_texture);
    }
}

TEST_CASE("Integration: receiver rejects torn committed frame/slot pair", "[integration]") {
    clear_all_registered_senders();

    nozzle::sender_desc desc{};
    desc.name = "integ_torn_publication_test";
    desc.application_name = "integ_app";
    desc.ring_buffer_size = 2;

    auto sender_result = nozzle::sender::create(desc);
    if (!sender_result.ok() && is_backend_unavailable(sender_result.error())) {
        SKIP("backend device is not available on this runner");
    }
    REQUIRE(sender_result.ok());
    auto snd = std::move(sender_result.value());

    nozzle::receiver_desc rdesc{};
    rdesc.name = "integ_torn_publication_test";

    auto recv_result = nozzle::receiver::create(rdesc);
    REQUIRE(recv_result.ok());
    auto recv = std::move(recv_result.value());

    nozzle::texture_desc texture_desc{};
    texture_desc.width = 8;
    texture_desc.height = 8;
    texture_desc.format = nozzle::texture_format::rgba8_unorm;

    auto first_result = snd.acquire_writable_frame(texture_desc);
    REQUIRE(first_result.ok());
    auto first = std::move(first_result.value());
    auto second_result = snd.acquire_writable_frame(texture_desc);
    REQUIRE(second_result.ok());
    auto second = std::move(second_result.value());

    REQUIRE(snd.commit_frame(first).ok());
    REQUIRE(snd.commit_frame(second).ok());

    auto state_result = open_writable_sender_state(snd.info().id);
    REQUIRE(state_result.ok());
    auto state_view = std::move(state_result.value());
    auto *state = static_cast<nozzle::detail::SenderSharedState *>(state_view.mapped);

    REQUIRE(state->slots[0].frame_number == 1);
    REQUIRE(state->slots[1].frame_number == 2);

    ipc::atomic_store_release_64(&state->committed_frame, 2);
    ipc::atomic_store_release_32(&state->committed_slot, 0);

    nozzle::acquire_desc acquire_desc{};
    acquire_desc.timeout_ms = 20;
    auto frame_result = recv.acquire_frame(acquire_desc);
    REQUIRE_FALSE(frame_result.ok());
    REQUIRE(frame_result.error().code == nozzle::ErrorCode::Timeout);
}

TEST_CASE("Integration: sender destruction removes from enumeration", "[integration]") {
    clear_all_registered_senders();

    nozzle::sender_desc desc{};
    desc.name = "integ_destroy_test";
    desc.application_name = "integ_app";

    {
        auto sender_result = nozzle::sender::create(desc);
        if (!sender_result.ok() && is_backend_unavailable(sender_result.error())) {
            SKIP("backend device is not available on this runner");
        }
        REQUIRE(sender_result.ok());

        auto senders = nozzle::enumerate_senders();
        REQUIRE(contains_sender_named(senders, "integ_destroy_test"));
    }

    auto senders_after = nozzle::enumerate_senders();
    REQUIRE_FALSE(contains_sender_named(senders_after, "integ_destroy_test"));
}

TEST_CASE("Integration: new receiver fails after sender destruction", "[integration]") {
    clear_all_registered_senders();

    nozzle::sender_desc desc{};
    desc.name = "integ_reconnect_fail_test";
    desc.application_name = "integ_app";

    {
        auto sender_result = nozzle::sender::create(desc);
        if (!sender_result.ok() && is_backend_unavailable(sender_result.error())) {
            SKIP("backend device is not available on this runner");
        }
        REQUIRE(sender_result.ok());
    }

    nozzle::receiver_desc rdesc{};
    rdesc.name = "integ_reconnect_fail_test";

    auto recv_result = nozzle::receiver::create(rdesc);
    REQUIRE(!recv_result.ok());
    REQUIRE(recv_result.error().code == nozzle::ErrorCode::SenderNotFound);
}

TEST_CASE("Integration: multiple senders, receiver connects to correct one", "[integration]") {
    clear_all_registered_senders();

    nozzle::sender_desc desc_a{};
    desc_a.name = "integ_sender_alpha";

    nozzle::sender_desc desc_b{};
    desc_b.name = "integ_sender_beta";

    auto ra = nozzle::sender::create(desc_a);
    if (!ra.ok() && is_backend_unavailable(ra.error())) {
        SKIP("backend device is not available on this runner");
    }
    REQUIRE(ra.ok());
    auto sa = std::move(ra.value());

    auto rb = nozzle::sender::create(desc_b);
    if (!rb.ok() && is_backend_unavailable(rb.error())) {
        SKIP("backend device is not available on this runner");
    }
    REQUIRE(rb.ok());
    auto sb = std::move(rb.value());

    SECTION("receiver targets alpha") {
        nozzle::receiver_desc rdesc{};
        rdesc.name = "integ_sender_alpha";

        auto recv_result = nozzle::receiver::create(rdesc);
        REQUIRE(recv_result.ok());

        auto conn = recv_result.value().connected_info();
        REQUIRE(conn.name == "integ_sender_alpha");
    }

    SECTION("receiver targets beta") {
        nozzle::receiver_desc rdesc{};
        rdesc.name = "integ_sender_beta";

        auto recv_result = nozzle::receiver::create(rdesc);
        REQUIRE(recv_result.ok());

        auto conn = recv_result.value().connected_info();
        REQUIRE(conn.name == "integ_sender_beta");
    }
}

TEST_CASE("Integration: enumerate_senders filters dead entries", "[integration]") {
    clear_all_registered_senders();

    nozzle::sender_desc desc{};
    desc.name = "integ_filter_test";
    desc.application_name = "integ_app";

    auto sender_result = nozzle::sender::create(desc);
    if (!sender_result.ok() && is_backend_unavailable(sender_result.error())) {
        SKIP("backend device is not available on this runner");
    }
    REQUIRE(sender_result.ok());
    auto snd = std::move(sender_result.value());

    auto senders = nozzle::enumerate_senders();
    REQUIRE(contains_sender_named(senders, "integ_filter_test"));

    snd = nozzle::sender{};

    auto senders_after = nozzle::enumerate_senders();
    REQUIRE_FALSE(contains_sender_named(senders_after, "integ_filter_test"));
}

TEST_CASE("Integration: metadata round-trip via shared state", "[integration]") {
    clear_all_registered_senders();

    nozzle::sender_desc desc{};
    desc.name = "integ_metadata_test";
    desc.application_name = "integ_app";

    auto sender_result = nozzle::sender::create(desc);
    if (!sender_result.ok() && is_backend_unavailable(sender_result.error())) {
        SKIP("backend device is not available on this runner");
    }
    REQUIRE(sender_result.ok());
    auto snd = std::move(sender_result.value());

    nozzle::metadata_list md;
    md.push_back({"color_space", "linear"});
    md.push_back({"channel_semantics", "depth"});

    auto set_result = snd.set_metadata(md);
    REQUIRE(set_result.ok());

    nozzle::receiver_desc rdesc{};
    rdesc.name = "integ_metadata_test";

    auto recv_result = nozzle::receiver::create(rdesc);
    REQUIRE(recv_result.ok());
    auto recv = std::move(recv_result.value());

    auto sender_md = recv.sender_metadata();
    REQUIRE(sender_md.size() == 2);
    REQUIRE(sender_md[0].key == "color_space");
    REQUIRE(sender_md[0].value == "linear");
    REQUIRE(sender_md[1].key == "channel_semantics");
    REQUIRE(sender_md[1].value == "depth");
}
