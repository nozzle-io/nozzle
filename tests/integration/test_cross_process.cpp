#include <bbb/nozzle/nozzle.hpp>

#include "registry.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>
#include <vector>

namespace registry = bbb::nozzle::detail::registry;

static void clear_all_registered_senders() {
    auto senders = bbb::nozzle::enumerate_senders();
    for (const auto &s : senders) {
        registry::unregister_sender(s.id.c_str());
    }
}

static bool contains_sender_named(
    const std::vector<bbb::nozzle::sender_info> &senders,
    const std::string &name)
{
    return std::any_of(senders.begin(), senders.end(),
        [&](const bbb::nozzle::sender_info &info) {
            return info.name == name;
        });
}

static bbb::nozzle::sender_info find_sender_named(
    const std::vector<bbb::nozzle::sender_info> &senders,
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

    bbb::nozzle::sender_desc desc{};
    desc.name = "integ_discover_test";
    desc.application_name = "integ_app";

    auto sender_result = bbb::nozzle::sender::create(desc);
    REQUIRE(sender_result.ok());
    auto snd = std::move(sender_result.value());

    SECTION("sender appears in enumeration") {
        auto senders = bbb::nozzle::enumerate_senders();
        REQUIRE_FALSE(senders.empty());
        REQUIRE(contains_sender_named(senders, "integ_discover_test"));

        auto info = find_sender_named(senders, "integ_discover_test");
        REQUIRE(info.application_name == "integ_app");
#if NOZZLE_HAS_METAL
        REQUIRE(info.backend == bbb::nozzle::backend_type::metal);
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

    bbb::nozzle::sender_desc desc{};
    desc.name = "integ_connect_test";
    desc.application_name = "integ_app";

    auto sender_result = bbb::nozzle::sender::create(desc);
    REQUIRE(sender_result.ok());
    auto snd = std::move(sender_result.value());

    bbb::nozzle::receiver_desc rdesc{};
    rdesc.name = "integ_connect_test";
    rdesc.application_name = "recv_app";

    auto recv_result = bbb::nozzle::receiver::create(rdesc);
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

    bbb::nozzle::sender_desc desc{};
    desc.name = "integ_noframe_test";
    desc.application_name = "integ_app";

    auto sender_result = bbb::nozzle::sender::create(desc);
    REQUIRE(sender_result.ok());
    auto snd = std::move(sender_result.value());

    bbb::nozzle::receiver_desc rdesc{};
    rdesc.name = "integ_noframe_test";

    auto recv_result = bbb::nozzle::receiver::create(rdesc);
    REQUIRE(recv_result.ok());
    auto recv = std::move(recv_result.value());

    auto frame_result = recv.acquire_frame();
    REQUIRE(!frame_result.ok());
    REQUIRE(frame_result.error().code == bbb::nozzle::ErrorCode::Timeout);
}

TEST_CASE("Integration: sender publish and receiver acquire", "[integration]") {
    clear_all_registered_senders();

    bbb::nozzle::sender_desc desc{};
    desc.name = "integ_publish_test";
    desc.application_name = "integ_app";

    auto sender_result = bbb::nozzle::sender::create(desc);
    REQUIRE(sender_result.ok());
    auto snd = std::move(sender_result.value());

    bbb::nozzle::receiver_desc rdesc{};
    rdesc.name = "integ_publish_test";

    auto recv_result = bbb::nozzle::receiver::create(rdesc);
    REQUIRE(recv_result.ok());
    auto recv = std::move(recv_result.value());

    SECTION("acquire_writable_frame and commit") {
        bbb::nozzle::texture_desc tdesc{};
        tdesc.width = 64;
        tdesc.height = 64;
        tdesc.format = bbb::nozzle::texture_format::rgba8_unorm;

        auto wf_result = snd.acquire_writable_frame(tdesc);
        REQUIRE(wf_result.ok());
        auto wf = std::move(wf_result.value());
        REQUIRE(wf.valid());

        auto commit_result = snd.commit_frame(wf);
        REQUIRE(commit_result.ok());
        REQUIRE(!wf.valid());
    }

    SECTION("receiver acquires published frame") {
        bbb::nozzle::texture_desc tdesc{};
        tdesc.width = 64;
        tdesc.height = 64;
        tdesc.format = bbb::nozzle::texture_format::rgba8_unorm;

        auto wf_result = snd.acquire_writable_frame(tdesc);
        REQUIRE(wf_result.ok());

        auto commit_result = snd.commit_frame(wf_result.value());
        REQUIRE(commit_result.ok());

        bbb::nozzle::acquire_desc adesc{};
        adesc.timeout_ms = 500;

        auto frame_result = recv.acquire_frame(adesc);
        REQUIRE(frame_result.ok());
        auto f = std::move(frame_result.value());
        REQUIRE(f.valid());
        REQUIRE(f.info().width == 64);
        REQUIRE(f.info().height == 64);
        REQUIRE(f.info().frame_index > 0);
        REQUIRE(f.info().transfer_mode_val == bbb::nozzle::transfer_mode::zero_copy_shared_texture);
    }
}

TEST_CASE("Integration: sender destruction removes from enumeration", "[integration]") {
    clear_all_registered_senders();

    bbb::nozzle::sender_desc desc{};
    desc.name = "integ_destroy_test";
    desc.application_name = "integ_app";

    {
        auto sender_result = bbb::nozzle::sender::create(desc);
        REQUIRE(sender_result.ok());

        auto senders = bbb::nozzle::enumerate_senders();
        REQUIRE(contains_sender_named(senders, "integ_destroy_test"));
    }

    auto senders_after = bbb::nozzle::enumerate_senders();
    REQUIRE_FALSE(contains_sender_named(senders_after, "integ_destroy_test"));
}

TEST_CASE("Integration: new receiver fails after sender destruction", "[integration]") {
    clear_all_registered_senders();

    bbb::nozzle::sender_desc desc{};
    desc.name = "integ_reconnect_fail_test";
    desc.application_name = "integ_app";

    {
        auto sender_result = bbb::nozzle::sender::create(desc);
        REQUIRE(sender_result.ok());
    }

    bbb::nozzle::receiver_desc rdesc{};
    rdesc.name = "integ_reconnect_fail_test";

    auto recv_result = bbb::nozzle::receiver::create(rdesc);
    REQUIRE(!recv_result.ok());
    REQUIRE(recv_result.error().code == bbb::nozzle::ErrorCode::SenderNotFound);
}

TEST_CASE("Integration: multiple senders, receiver connects to correct one", "[integration]") {
    clear_all_registered_senders();

    bbb::nozzle::sender_desc desc_a{};
    desc_a.name = "integ_sender_alpha";

    bbb::nozzle::sender_desc desc_b{};
    desc_b.name = "integ_sender_beta";

    auto ra = bbb::nozzle::sender::create(desc_a);
    REQUIRE(ra.ok());
    auto sa = std::move(ra.value());

    auto rb = bbb::nozzle::sender::create(desc_b);
    REQUIRE(rb.ok());
    auto sb = std::move(rb.value());

    SECTION("receiver targets alpha") {
        bbb::nozzle::receiver_desc rdesc{};
        rdesc.name = "integ_sender_alpha";

        auto recv_result = bbb::nozzle::receiver::create(rdesc);
        REQUIRE(recv_result.ok());

        auto conn = recv_result.value().connected_info();
        REQUIRE(conn.name == "integ_sender_alpha");
    }

    SECTION("receiver targets beta") {
        bbb::nozzle::receiver_desc rdesc{};
        rdesc.name = "integ_sender_beta";

        auto recv_result = bbb::nozzle::receiver::create(rdesc);
        REQUIRE(recv_result.ok());

        auto conn = recv_result.value().connected_info();
        REQUIRE(conn.name == "integ_sender_beta");
    }
}

TEST_CASE("Integration: enumerate_senders filters dead entries", "[integration]") {
    clear_all_registered_senders();

    bbb::nozzle::sender_desc desc{};
    desc.name = "integ_filter_test";
    desc.application_name = "integ_app";

    auto sender_result = bbb::nozzle::sender::create(desc);
    REQUIRE(sender_result.ok());
    auto snd = std::move(sender_result.value());

    auto senders = bbb::nozzle::enumerate_senders();
    REQUIRE(contains_sender_named(senders, "integ_filter_test"));

    snd = bbb::nozzle::sender{};

    auto senders_after = bbb::nozzle::enumerate_senders();
    REQUIRE_FALSE(contains_sender_named(senders_after, "integ_filter_test"));
}

TEST_CASE("Integration: metadata round-trip via shared state", "[integration]") {
    clear_all_registered_senders();

    bbb::nozzle::sender_desc desc{};
    desc.name = "integ_metadata_test";
    desc.application_name = "integ_app";

    auto sender_result = bbb::nozzle::sender::create(desc);
    REQUIRE(sender_result.ok());
    auto snd = std::move(sender_result.value());

    bbb::nozzle::metadata_list md;
    md.push_back({"color_space", "linear"});
    md.push_back({"channel_semantics", "depth"});

    auto set_result = snd.set_metadata(md);
    REQUIRE(set_result.ok());

    bbb::nozzle::receiver_desc rdesc{};
    rdesc.name = "integ_metadata_test";

    auto recv_result = bbb::nozzle::receiver::create(rdesc);
    REQUIRE(recv_result.ok());
    auto recv = std::move(recv_result.value());

    auto sender_md = recv.sender_metadata();
    REQUIRE(sender_md.size() == 2);
    REQUIRE(sender_md[0].key == "color_space");
    REQUIRE(sender_md[0].value == "linear");
    REQUIRE(sender_md[1].key == "channel_semantics");
    REQUIRE(sender_md[1].value == "depth");
}
