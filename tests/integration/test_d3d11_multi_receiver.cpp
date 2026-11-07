#include <nozzle/nozzle.hpp>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <iostream>
#include <string>
#include <utility>

namespace {

using clock_type = std::chrono::steady_clock;

double elapsed_ms(clock_type::time_point start) {
    return std::chrono::duration<double, std::milli>(clock_type::now() - start).count();
}

nozzle::Result<nozzle::frame> measure_acquire(
    nozzle::receiver &receiver, const char *phase, uint32_t timeout_ms) {
    nozzle::acquire_desc desc{};
    desc.timeout_ms = timeout_ms;
    const auto start = clock_type::now();
    auto result = receiver.acquire_frame(desc);
    const auto duration = elapsed_ms(start);
    std::cout << "D3D11_MEASUREMENT phase=" << phase
              << " timeout_ms=" << timeout_ms << " elapsed_ms=" << duration
              << " code=" << static_cast<int>(result.ok() ? nozzle::ErrorCode::Ok : result.error().code)
              << " frame=" << (result.ok() ? result.value().info().frame_index : 0)
              << " message=" << (result.ok() ? "OK" : result.error().message) << std::endl;
    REQUIRE((result.ok() || result.error().code == nozzle::ErrorCode::Timeout));
    return result;
}

struct publication {
    bool ok;
    bool acquired;
    uint32_t slot;
};

publication measure_publish(nozzle::sender &sender, const char *phase) {
    nozzle::texture_desc desc{};
    desc.width = 64;
    desc.height = 64;
    desc.format = nozzle::texture_format::rgba8_unorm;
    const auto acquire_start = clock_type::now();
    auto writable = sender.acquire_writable_frame(desc);
    const auto acquire_duration = elapsed_ms(acquire_start);
    std::cout << "D3D11_MEASUREMENT phase=" << phase << " stage=acquire"
              << " elapsed_ms=" << acquire_duration
              << " code=" << static_cast<int>(writable.ok() ? nozzle::ErrorCode::Ok : writable.error().code)
              << " message=" << (writable.ok() ? "OK" : writable.error().message) << std::endl;
    REQUIRE((writable.ok() || writable.error().code == nozzle::ErrorCode::Timeout));
    if (!writable.ok()) return {false, false, UINT32_MAX};
    const auto slot = nozzle::detail::get_writable_frame_slot(writable.value());
    // No pixel writes: this measures the publication/ownership handshake only.
    const auto start = clock_type::now();
    auto result = sender.commit_frame(writable.value());
    const auto duration = elapsed_ms(start);
    std::cout << "D3D11_MEASUREMENT phase=" << phase << " stage=commit slot=" << slot
              << " elapsed_ms=" << duration
              << " code=" << static_cast<int>(result.ok() ? nozzle::ErrorCode::Ok : result.error().code)
              << " message=" << (result.ok() ? "OK" : result.error().message) << std::endl;
    REQUIRE((result.ok() || result.error().code == nozzle::ErrorCode::Timeout));
    // Some error paths consume the writable frame, others leave a reservation.
    if (!result.ok() && writable.value().valid()) {
        REQUIRE(sender.discard_frame(writable.value()).ok());
    }
    return {result.ok(), true, slot};
}

} // namespace

// Explicitly selected by Windows CI, not duplicated by the default regression run.
// Success means measurement completed, NOT that broadcast/fairness is correct.
TEST_CASE("D3D11 multi-receiver handshake measurements", "[.][d3d11_multi_receiver]") {
    for (const uint32_t ring_size : {1u, 3u}) {
        INFO("ring_size=" << ring_size);
        const auto nonce = clock_type::now().time_since_epoch().count();
        nozzle::sender_desc sender_desc{};
        sender_desc.name = "measure_fanout_" + std::to_string(nonce) + "_" + std::to_string(ring_size);
        sender_desc.ring_buffer_size = ring_size;
        auto sender_result = nozzle::sender::create(sender_desc);
        // Missing backend is a failed measurement, not SKIP evidence.
        REQUIRE(sender_result.ok());
        auto sender = std::move(sender_result.value());
        REQUIRE(sender.info().backend == nozzle::backend_type::d3d11);
        nozzle::receiver_desc receiver_desc{};
        receiver_desc.name = sender_desc.name;
        auto a_result = nozzle::receiver::create(receiver_desc);
        auto b_result = nozzle::receiver::create(receiver_desc);
        REQUIRE(a_result.ok());
        REQUIRE(b_result.ok());
        auto receiver_a = std::move(a_result.value());
        auto receiver_b = std::move(b_result.value());
        std::cout << "D3D11_MEASUREMENT begin ring_size=" << ring_size
                  << " scope=same_process_handshake_only receivers=2"
                  << " driver_request=hardware physical_gpu_unverified=1" << std::endl;

        REQUIRE(measure_publish(sender, "initial_publish").ok);
        auto a_first = measure_acquire(receiver_a, "a_first", 100);
        REQUIRE(a_first.ok());
        const auto first_frame = a_first.value().info().frame_index;
        a_first.value().release();
        bool b_consumed_first = false;
        {
            auto b_finite = measure_acquire(receiver_b, "b_same_frame_finite", 20);
            b_consumed_first = b_finite.ok();
            if (b_finite.ok()) REQUIRE(b_finite.value().info().frame_index == first_frame);
        }
        // If fan-out becomes supported, reset B so default timeout still probes
        // this published frame rather than B's "no new frame" fast path.
        if (b_consumed_first) {
            auto replacement = nozzle::receiver::create(receiver_desc);
            REQUIRE(replacement.ok());
            receiver_b = std::move(replacement.value());
        }
        {
            auto b_default = measure_acquire(receiver_b, "b_same_frame_default", 0);
            if (b_default.ok()) REQUIRE(b_default.value().info().frame_index == first_frame);
        }
        REQUIRE(measure_publish(sender, "republish_for_b").ok);
        {
            auto b_recovered = measure_acquire(receiver_b, "b_recovery", 100);
            REQUIRE(b_recovered.ok());
            REQUIRE(b_recovered.value().info().frame_index > first_frame);
        }

        const auto held_publication = measure_publish(sender, "publish_for_a_hold");
        REQUIRE(held_publication.ok);
        auto held = measure_acquire(receiver_a, "a_hold", 100);
        REQUIRE(held.ok());
        const auto held_frame = held.value().info().frame_index;
        uint32_t successful_commits = 0;
        uint32_t timed_out_commits = 0;
        uint32_t timed_out_acquires = 0;
        bool visited_held_slot = false;
        for (uint32_t i = 0; i < ring_size; ++i) {
            const auto published = measure_publish(sender, "publish_while_a_holds");
            visited_held_slot = visited_held_slot || published.slot == held_publication.slot;
            if (!published.acquired) ++timed_out_acquires;
            else if (published.ok) ++successful_commits;
            else ++timed_out_commits;
        }
        held.value().release();
        REQUIRE(measure_publish(sender, "publish_after_a_release").ok);
        {
            auto recovered = measure_acquire(receiver_b, "b_after_a_release", 100);
            REQUIRE(recovered.ok());
            REQUIRE(recovered.value().info().frame_index > held_frame);
        }
        std::cout << "D3D11_MEASUREMENT complete ring_size=" << ring_size
                  << " held_slot=" << held_publication.slot
                  << " held_frame=" << held_frame
                  << " held_successful_commits=" << successful_commits
                  << " held_timed_out_commits=" << timed_out_commits
                  << " held_timed_out_acquires=" << timed_out_acquires
                  << " held_slot_selected=" << visited_held_slot
                  << " broadcast_correctness_claim=0" << std::endl;
    }
}
