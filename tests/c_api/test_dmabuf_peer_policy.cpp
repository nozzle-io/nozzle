// nozzle - test_dmabuf_peer_policy.cpp - real CPU-only broker peer tests

#include <catch2/catch_test_macros.hpp>
#include <nozzle/config.hpp>

#if NOZZLE_HAS_DMA_BUF
#include "backends/backend_dispatch.hpp"

#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>

#include <grp.h>
#include <linux/memfd.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {

constexpr uid_t owner_uid = 65534;
constexpr uid_t foreign_uid = 65533;
constexpr char payload[] = "nozzle peer policy content\n";

class scoped_fd {
public:
    explicit scoped_fd(int fd = -1) : fd_{fd} {}
    ~scoped_fd() { reset(); }
    scoped_fd(const scoped_fd &) = delete;
    scoped_fd &operator=(const scoped_fd &) = delete;
    int get() const { return fd_; }
    void reset() {
        if (0 <= fd_) close(fd_);
        fd_ = -1;
    }
private:
    int fd_;
};

class scoped_child {
public:
    explicit scoped_child(pid_t pid, bool group = false) : pid_{pid}, group_{group} {}
    ~scoped_child() {
        if (0 < pid_) {
            // A dropped parent cannot kill a foreign-UID child. That child has
            // its own six-second alarm, so reaping still has a hard deadline.
            if (group_) kill(-pid_, SIGKILL);
            // Also cover cleanup before the child has completed setpgid().
            kill(pid_, SIGKILL);
            int status{};
            while (waitpid(pid_, &status, 0) < 0 && errno == EINTR) {}
        }
    }
    scoped_child(const scoped_child &) = delete;
    scoped_child &operator=(const scoped_child &) = delete;
    bool finish() {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{3};
        do {
            int status{};
            const pid_t result = waitpid(pid_, &status, WNOHANG);
            if (result == pid_) {
                pid_ = -1;
                return WIFEXITED(status) && WEXITSTATUS(status) == 0;
            }
            if (result < 0 && errno != EINTR) return false;
            std::this_thread::sleep_for(std::chrono::milliseconds{10});
        } while (std::chrono::steady_clock::now() < deadline);
        return false;
    }
private:
    pid_t pid_;
    bool group_;
};

struct observation {
    uid_t euid{static_cast<uid_t>(-1)};
    uid_t peer_euid{static_cast<uid_t>(-1)};
    bool connected{false};
    bool requested{false};
    bool received_fd{false};
    bool content_matches{false};
    char error[96]{};
};

struct scenario_result {
    uid_t euid{static_cast<uid_t>(-1)};
    observation peer{};
    observation followup{};
    bool lookup_succeeded{false};
    bool cached_fd{false};
    char error[96]{};
};

template <typename T>
T fail(T result, const char *message) {
    std::snprintf(result.error, sizeof(result.error), "%s (errno=%d)", message, errno);
    return result;
}

bool ready(int fd, int timeout_ms) {
    pollfd item{fd, POLLIN, 0};
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds{timeout_ms};
    do {
        const int result = poll(&item, 1, timeout_ms);
        if (0 <= result || errno != EINTR) return 0 < result && (item.revents & POLLIN) != 0;
        timeout_ms = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now()).count());
    } while (0 < timeout_ms);
    return false;
}

template <typename T>
bool send_packet(int fd, const T &value) {
    return send(fd, &value, sizeof(value), MSG_NOSIGNAL) == static_cast<ssize_t>(sizeof(value));
}

template <typename T>
bool receive_packet(int fd, T &value, int timeout_ms = 3000) {
    return ready(fd, timeout_ms) &&
        recv(fd, &value, sizeof(value), 0) == static_cast<ssize_t>(sizeof(value));
}

void arm_deadline(unsigned seconds) {
    signal(SIGALRM, SIG_DFL);
    sigset_t signals;
    sigemptyset(&signals);
    sigaddset(&signals, SIGALRM);
    sigprocmask(SIG_UNBLOCK, &signals, nullptr);
    alarm(seconds);
}

bool become_user(uid_t uid) {
    if (geteuid() == 0) {
        if (setgroups(0, nullptr) != 0 || setgid(static_cast<gid_t>(uid)) != 0 || setuid(uid) != 0) {
            return false;
        }
    }
    return uid != 0 && getuid() == uid && geteuid() == uid;
}

bool set_timeouts(int fd) {
    timeval timeout{};
    timeout.tv_sec = 2;
    return setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) == 0 &&
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) == 0;
}

bool read_peer_uid(int fd, uid_t &uid) {
    ucred credentials{};
    socklen_t size = sizeof(credentials);
    if (getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &credentials, &size) != 0 ||
        size != sizeof(credentials)) return false;
    uid = credentials.uid;
    return true;
}

int make_payload() {
    const int fd = static_cast<int>(syscall(SYS_memfd_create, "nozzle-peer-policy", MFD_CLOEXEC));
    if (fd < 0) return -1;
    if (write(fd, payload, sizeof(payload)) != static_cast<ssize_t>(sizeof(payload))) {
        close(fd);
        return -1;
    }
    return fd;
}

observation request_fd(const nozzle::detail::backend::abstract_socket_addr &address) {
    observation result{};
    result.euid = geteuid();
    scoped_fd client{socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0)};
    if (client.get() < 0 || !set_timeouts(client.get())) return fail(result, "client socket");
    if (connect(client.get(), reinterpret_cast<const sockaddr *>(&address.addr), address.len) != 0) {
        return fail(result, "connect");
    }
    result.connected = true;
    if (!read_peer_uid(client.get(), result.peer_euid)) return fail(result, "server credentials");
    const uint32_t slot = 0;
    const ssize_t sent = send(client.get(), &slot, sizeof(slot), MSG_NOSIGNAL);
    result.requested = sent == static_cast<ssize_t>(sizeof(slot));
    if (!result.requested && !(sent < 0 && (errno == EPIPE || errno == ECONNRESET))) {
        return fail(result, "request write");
    }
    errno = 0;
    scoped_fd received{nozzle::detail::linux_backend::recv_fd_response(client.get())};
    result.received_fd = 0 <= received.get();
    if (!result.received_fd && (errno == EAGAIN || errno == EWOULDBLOCK || errno == ETIMEDOUT)) {
        return fail(result, "denial must close, not time out");
    }
    if (result.received_fd) {
        char bytes[sizeof(payload)]{};
        result.content_matches = pread(received.get(), bytes, sizeof(bytes), 0) ==
            static_cast<ssize_t>(sizeof(bytes)) && std::memcmp(bytes, payload, sizeof(bytes)) == 0;
    }
    return result;
}

class local_broker {
public:
    ~local_broker() {
        {
            std::lock_guard<std::mutex> lock(state_.retry_mutex_);
            state_.stop_flag_.store(true);
        }
        state_.retry_cv_.notify_all();
        if (0 <= state_.listen_fd_) shutdown(state_.listen_fd_, SHUT_RDWR);
        if (state_.accept_thread_.joinable()) state_.accept_thread_.join();
        if (0 <= state_.listen_fd_) close(state_.listen_fd_);
    }
    bool start(const nozzle::detail::backend::abstract_socket_addr &address, int source_fd) {
        state_.listen_fd_ = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
        if (state_.listen_fd_ < 0 ||
            bind(state_.listen_fd_, reinterpret_cast<const sockaddr *>(&address.addr), address.len) != 0 ||
            listen(state_.listen_fd_, 4) != 0) return false;
        state_.slot_fds_[0] = source_fd;
        state_.accept_thread_ = std::thread(&nozzle::detail::backend::run_sender_accept_loop,
            static_cast<void *>(&state_));
        return true;
    }
private:
    nozzle::detail::backend::linux_sender_state state_{};
};

scenario_result sender_scenario(uid_t server_uid, uid_t client_uid) {
    scenario_result result{};
    const std::string uuid = "peer-send-" + std::to_string(getpid());
    const nozzle::detail::backend::abstract_socket_addr address{"nozzle_fd_" + uuid};
    int channels[2];
    if (socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, channels) != 0) {
        return fail(result, "client sidechannel");
    }
    scoped_fd parent_channel{channels[0]};
    scoped_fd child_channel{channels[1]};
    const pid_t pid = fork();
    if (pid < 0) return fail(result, "client fork");
    if (pid == 0) {
        parent_channel.reset();
        arm_deadline(6);
        observation peer{};
        if (!become_user(client_uid)) {
            peer = fail(peer, "client privilege drop");
        } else {
            peer.euid = geteuid();
            char go{};
            if (!receive_packet(child_channel.get(), go) || go != 'G') peer = fail(peer, "broker ready");
            else peer = request_fd(address);
        }
        const bool sent = send_packet(child_channel.get(), peer);
        _exit(sent ? 0 : 1);
    }
    scoped_child child{pid};
    child_channel.reset();
    if (!become_user(server_uid)) return fail(result, "broker privilege drop");
    result.euid = geteuid();
    // Neither the payload FD nor broker state/thread exists at client fork.
    scoped_fd source{make_payload()};
    if (source.get() < 0) return fail(result, "payload");
    local_broker broker;
    if (!broker.start(address, source.get())) return fail(result, "broker listen");
    if (!send_packet(parent_channel.get(), 'G') || !receive_packet(parent_channel.get(), result.peer)) {
        return fail(result, "client report");
    }
    if (!child.finish()) return fail(result, "client exit");
    result.followup = request_fd(address);
    return result;
}

observation fake_server(int channel, const nozzle::detail::backend::abstract_socket_addr &address) {
    observation result{};
    if (!become_user(foreign_uid)) return fail(result, "fake server privilege drop");
    result.euid = geteuid();
    scoped_fd listener{socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0)};
    if (listener.get() < 0 ||
        bind(listener.get(), reinterpret_cast<const sockaddr *>(&address.addr), address.len) != 0 ||
        listen(listener.get(), 1) != 0) return fail(result, "fake server listen");
    if (!send_packet(channel, 'R') || !ready(listener.get(), 3000)) return fail(result, "fake server accept wait");
    scoped_fd client{accept(listener.get(), nullptr, nullptr)};
    if (client.get() < 0 || !set_timeouts(client.get())) return fail(result, "fake server accept");
    result.connected = true;
    if (!read_peer_uid(client.get(), result.peer_euid)) return fail(result, "receiver credentials");
    uint32_t slot{};
    const ssize_t size = recv(client.get(), &slot, sizeof(slot), MSG_WAITALL);
    result.requested = 0 < size;
    if (size < 0 && errno != ECONNRESET) return fail(result, "fake server request read");
    if (0 < size) {
        if (size != static_cast<ssize_t>(sizeof(slot)) || slot != 0) return fail(result, "unexpected slot request");
        scoped_fd source{make_payload()};
        if (source.get() < 0 || !nozzle::detail::linux_backend::send_fd_response(client.get(), source.get())) {
            return fail(result, "fake server fd response");
        }
    }
    return result;
}

scenario_result receiver_scenario() {
    scenario_result result{};
    const std::string uuid = "peer-recv-" + std::to_string(getpid());
    const nozzle::detail::backend::abstract_socket_addr address{"nozzle_fd_" + uuid};
    int channels[2];
    if (socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, channels) != 0) {
        return fail(result, "server sidechannel");
    }
    scoped_fd parent_channel{channels[0]};
    scoped_fd child_channel{channels[1]};
    const pid_t pid = fork();
    if (pid < 0) return fail(result, "server fork");
    if (pid == 0) {
        parent_channel.reset();
        arm_deadline(6);
        const auto peer = fake_server(child_channel.get(), address);
        const bool sent = send_packet(child_channel.get(), peer);
        _exit(sent ? 0 : 1);
    }
    scoped_child child{pid};
    child_channel.reset();
    if (!become_user(owner_uid)) return fail(result, "receiver privilege drop");
    result.euid = geteuid();
    char go{};
    if (!receive_packet(parent_channel.get(), go) || go != 'R') return fail(result, "fake server ready");
    auto &cache = nozzle::detail::backend::get_receiver_cache();
    cache.cache_.clear();
    // Unknown format guarantees no EGL/GPU work even if the unauthorized FD
    // is cached. A failed texture import alone is not a peer-policy oracle.
    auto lookup = nozzle::detail::backend::lookup_texture(nullptr, uint64_t{1} << 32,
        1, 1, static_cast<uint32_t>(nozzle::texture_format::unknown), 0,
        0, 0, 0, 0, 0, nullptr, nullptr, static_cast<uint64_t>(pid), uuid.c_str());
    result.lookup_succeeded = lookup.ok();
    result.cached_fd = 0 <= cache.cache_.get_fd(0);
    cache.cache_.clear();
    if (!receive_packet(parent_channel.get(), result.peer)) return fail(result, "fake server report");
    if (!child.finish()) return fail(result, "fake server exit");
    return result;
}

scenario_result run_scenario(bool reverse, uid_t server_uid, uid_t client_uid) {
    scenario_result result{};
    // Catch runs these cases serially. Quiesce any broker from earlier cases
    // before fork; scenario senders use fresh local state, never the global.
    nozzle::detail::backend::cleanup_sender_socket();
    {
        auto &cache = nozzle::detail::backend::get_receiver_cache();
        std::lock_guard<std::mutex> lock(cache.mutex_);
        cache.cache_.clear();
    }
    int channels[2];
    if (socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, channels) != 0) {
        return fail(result, "scenario sidechannel");
    }
    scoped_fd parent_channel{channels[0]};
    scoped_fd child_channel{channels[1]};
    const pid_t pid = fork();
    if (pid < 0) return fail(result, "scenario fork");
    if (pid == 0) {
        parent_channel.reset();
        if (setpgid(0, 0) != 0) _exit(1);
        arm_deadline(10);
        result = reverse ? receiver_scenario() : sender_scenario(server_uid, client_uid);
        const bool sent = send_packet(child_channel.get(), result);
        _exit(sent ? 0 : 1);
    }
    scoped_child child{pid, true};
    child_channel.reset();
    if (!receive_packet(parent_channel.get(), result, 12000)) return fail(result, "scenario report/deadline");
    if (!child.finish()) return fail(result, "scenario exit");
    return result;
}

void check_sender_setup(const scenario_result &result, uid_t server_uid, uid_t client_uid) {
    INFO("scenario error: " << result.error << "; client error: " << result.peer.error
        << "; follow-up error: " << result.followup.error);
    INFO("broker EUID=" << result.euid << ", client EUID=" << result.peer.euid
        << ", observed broker EUID=" << result.peer.peer_euid
        << ", follow-up EUID=" << result.followup.euid);
    REQUIRE(result.error[0] == '\0');
    REQUIRE(result.peer.error[0] == '\0');
    CHECK(result.euid == server_uid);
    CHECK(result.peer.euid == client_uid);
    CHECK(result.peer.peer_euid == server_uid);
    CHECK(result.peer.connected);
    CHECK(result.followup.error[0] == '\0');
    CHECK(result.followup.euid == server_uid);
    CHECK(result.followup.peer_euid == server_uid);
    CHECK(result.followup.connected);
    CHECK(result.followup.requested);
    CHECK(result.followup.received_fd);
    CHECK(result.followup.content_matches);
}

TEST_CASE("DMA-BUF broker serves a separate same-EUID process and follow-up", "[fd_broker][peer_policy]") {
    const uid_t uid = geteuid() == 0 ? owner_uid : geteuid();
    const auto result = run_scenario(false, uid, uid);
    check_sender_setup(result, uid, uid);
    CHECK(result.peer.requested);
    CHECK(result.peer.received_fd);
    CHECK(result.peer.content_matches);
}

TEST_CASE("DMA-BUF broker denies a different ordinary UID and stays usable", "[.][peer_policy_privileged]") {
    REQUIRE(geteuid() == 0);
    const auto result = run_scenario(false, owner_uid, foreign_uid);
    check_sender_setup(result, owner_uid, foreign_uid);
    if (result.peer.received_fd) CHECK(result.peer.content_matches);
    CHECK_FALSE(result.peer.received_fd);
}

TEST_CASE("DMA-BUF lookup rejects a different-UID server before request or cache", "[.][peer_policy_privileged]") {
    REQUIRE(geteuid() == 0);
    const auto result = run_scenario(true, owner_uid, foreign_uid);
    INFO("scenario error: " << result.error << "; server error: " << result.peer.error);
    INFO("receiver EUID=" << result.euid << ", fake server EUID=" << result.peer.euid
        << ", observed receiver EUID=" << result.peer.peer_euid);
    REQUIRE(result.error[0] == '\0');
    REQUIRE(result.peer.error[0] == '\0');
    CHECK(result.euid == owner_uid);
    CHECK(result.peer.euid == foreign_uid);
    CHECK(result.peer.peer_euid == owner_uid);
    CHECK(result.peer.connected);
    CHECK_FALSE(result.peer.requested);
    CHECK_FALSE(result.cached_fd);
    CHECK_FALSE(result.lookup_succeeded);
}

} // namespace
#endif
