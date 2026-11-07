// nozzle - test_dmabuf_peer_query.cpp - injected peer credential failures

#include <catch2/catch_test_macros.hpp>
#include <nozzle/config.hpp>

#if NOZZLE_HAS_DMA_BUF && NOZZLE_ENABLE_TEST_HOOKS
#include "backends/backend_dispatch.hpp"

#include <array>
#include <cerrno>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {

class scoped_peer_fd {
public:
    explicit scoped_peer_fd(int fd) : fd_{fd} {}
    ~scoped_peer_fd() { if (0 <= fd_) close(fd_); }
    scoped_peer_fd(const scoped_peer_fd &) = delete;
    scoped_peer_fd &operator=(const scoped_peer_fd &) = delete;
    auto get() const -> int { return fd_; }
    auto release() -> int { const int fd = fd_; fd_ = -1; return fd; }
private:
    int fd_;
};

auto descriptor_count() -> size_t {
    DIR *directory = opendir("/proc/self/fd");
    if (!directory) return 0;
    size_t count = 0;
    while (readdir(directory)) ++count;
    closedir(directory);
    return count;
}

enum class query_failure { syscall, short_result, invalid_pid, invalid_uid, invalid_gid };

struct query_sequence {
    query_failure failure;
    unsigned calls{0};
};

auto fail_first_query(int fd, struct ucred *credentials, socklen_t *length, void *context) -> int {
    auto &sequence = *static_cast<query_sequence *>(context);
    if (sequence.calls++ != 0) {
        return getsockopt(fd, SOL_SOCKET, SO_PEERCRED, credentials, length);
    }
    if (sequence.failure == query_failure::syscall) {
        errno = EIO;
        return -1;
    }
    *credentials = {getpid(), geteuid(), getegid()};
    switch (sequence.failure) {
        case query_failure::short_result: *length = sizeof(*credentials) - 1; break;
        case query_failure::invalid_pid: credentials->pid = 0; break;
        case query_failure::invalid_uid: credentials->uid = static_cast<uid_t>(-1); break;
        case query_failure::invalid_gid: credentials->gid = static_cast<gid_t>(-1); break;
        case query_failure::syscall: break;
    }
    return 0;
}

struct accept_sequence {
    nozzle::detail::backend::linux_sender_state *state;
    std::array<int, 2> descriptors;
    size_t index{0};
};

auto accept_two_clients(int, void *context) -> int {
    auto &sequence = *static_cast<accept_sequence *>(context);
    if (sequence.index < sequence.descriptors.size()) {
        return sequence.descriptors[sequence.index++];
    }
    sequence.state->stop_flag_.store(true, std::memory_order_relaxed);
    errno = EBADF;
    return -1;
}

} // namespace

TEST_CASE("DMA-BUF peer query failures deny one client without disabling the broker",
          "[dmabuf][fd_broker][peer_policy]") {
    const auto before = descriptor_count();
    REQUIRE(0 < before);
    for (auto failure : {query_failure::syscall, query_failure::short_result,
                         query_failure::invalid_pid, query_failure::invalid_uid,
                         query_failure::invalid_gid}) {
        for (unsigned repetition = 0; repetition < 4; ++repetition) {
            INFO("injected failure=" << static_cast<int>(failure) << " repetition=" << repetition);
            nozzle::detail::backend::linux_sender_state state;
            scoped_peer_fd source{open("/dev/null", O_RDONLY | O_CLOEXEC)};
            REQUIRE(0 <= source.get());
            state.slot_fds_[0] = source.get();
            int denied_pair[2];
            int accepted_pair[2];
            REQUIRE(socketpair(AF_UNIX, SOCK_STREAM, 0, denied_pair) == 0);
            scoped_peer_fd denied_server{denied_pair[0]};
            scoped_peer_fd denied_client{denied_pair[1]};
            REQUIRE(socketpair(AF_UNIX, SOCK_STREAM, 0, accepted_pair) == 0);
            scoped_peer_fd accepted_server{accepted_pair[0]};
            scoped_peer_fd accepted_client{accepted_pair[1]};
            const struct timeval timeout{1, 0};
            REQUIRE(setsockopt(denied_client.get(), SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) == 0);
            REQUIRE(setsockopt(accepted_client.get(), SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) == 0);
            const uint32_t slot = 0;
            REQUIRE(send(denied_client.get(), &slot, sizeof(slot), MSG_NOSIGNAL) == sizeof(slot));
            REQUIRE(send(accepted_client.get(), &slot, sizeof(slot), MSG_NOSIGNAL) == sizeof(slot));
            query_sequence queries{failure};
            accept_sequence accepts{&state, {denied_server.release(), accepted_server.release()}};
            state.test_accept_ = &accept_two_clients;
            state.test_accept_context_ = &accepts;
            state.test_peer_query_ = &fail_first_query;
            state.test_peer_query_context_ = &queries;
            nozzle::detail::backend::run_sender_accept_loop(&state);
            CHECK(queries.calls == 2);
            CHECK(accepts.index == 2);
            CHECK(fcntl(denied_pair[0], F_GETFD) == -1);
            CHECK(fcntl(accepted_pair[0], F_GETFD) == -1);
            scoped_peer_fd denied{nozzle::detail::linux_backend::recv_fd_response(denied_client.get())};
            CHECK(denied.get() == -1);
            scoped_peer_fd accepted{nozzle::detail::linux_backend::recv_fd_response(accepted_client.get())};
            REQUIRE(0 <= accepted.get());
            struct stat source_stat{};
            struct stat accepted_stat{};
            REQUIRE(fstat(source.get(), &source_stat) == 0);
            REQUIRE(fstat(accepted.get(), &accepted_stat) == 0);
            CHECK(source_stat.st_dev == accepted_stat.st_dev);
            CHECK(source_stat.st_ino == accepted_stat.st_ino);
            CHECK(nozzle::detail::backend::sender_broker_status(&state).ok());
        }
    }
    CHECK(descriptor_count() == before);
}

TEST_CASE("DMA-BUF receiver rejects credential query errors before sending a slot",
          "[dmabuf][fd_broker][peer_policy]") {
    auto &cache = nozzle::detail::backend::get_receiver_cache();
    struct reset_query {
        ~reset_query() {
            auto &target = nozzle::detail::backend::get_receiver_cache();
            target.test_peer_query_ = nullptr;
            target.test_peer_query_context_ = nullptr;
        }
    } reset;
    unsigned sequence = 0;
    for (auto failure : {query_failure::syscall, query_failure::short_result}) {
        query_sequence queries{failure};
        cache.test_peer_query_ = &fail_first_query;
        cache.test_peer_query_context_ = &queries;
        const std::string uuid = "peer-query-" + std::to_string(getpid()) + "-" + std::to_string(sequence++);
        nozzle::detail::backend::abstract_socket_addr address{"nozzle_fd_" + uuid};
        scoped_peer_fd listener{socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0)};
        REQUIRE(0 <= listener.get());
        REQUIRE(bind(listener.get(), reinterpret_cast<struct sockaddr *>(&address.addr), address.len) == 0);
        REQUIRE(listen(listener.get(), 1) == 0);
        auto result = nozzle::detail::backend::lookup_texture(
            nullptr, 0, 16, 16, 0, 0, 0, 0, 0, 0, 0, nullptr, nullptr, 0, uuid.c_str());
        CHECK(!result.ok());
        CHECK(queries.calls == 1);
        scoped_peer_fd client{accept(listener.get(), nullptr, nullptr)};
        REQUIRE(0 <= client.get());
        uint32_t slot = 99;
        CHECK(recv(client.get(), &slot, sizeof(slot), MSG_DONTWAIT) == 0);
        CHECK(!cache.cache_.has(0, uuid.c_str(), 16, 16, 0, 0, 0));
    }
}
#endif
