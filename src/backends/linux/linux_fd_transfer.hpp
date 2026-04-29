#pragma once

#if NOZZLE_HAS_DMA_BUF

#include <array>
#include <cstdint>
#include <mutex>
#include <string>

namespace nozzle::detail::linux_backend {

bool send_fds(int socket_fd, const int *fds, size_t count);

bool recv_fds(int socket_fd, int *fds, size_t count);

class fd_sender {
public:
    fd_sender();
    ~fd_sender();

    fd_sender(const fd_sender &) = delete;
    fd_sender &operator=(const fd_sender &) = delete;
    fd_sender(fd_sender &&other) noexcept;
    fd_sender &operator=(fd_sender &&other) noexcept;

    bool start(const char *uuid);
    void stop();
    bool send_dmabuf_fds(const int *fds, size_t count);
    bool is_listening() const;

private:
    int listen_fd_{-1};
    std::string socket_path_{};
    bool listening_{false};
};

class fd_receiver {
public:
    fd_receiver();
    ~fd_receiver();

    fd_receiver(const fd_receiver &) = delete;
    fd_receiver &operator=(const fd_receiver &) = delete;
    fd_receiver(fd_receiver &&other) noexcept;
    fd_receiver &operator=(fd_receiver &&other) noexcept;

    bool connect(const char *uuid);
    void disconnect();
    bool recv_dmabuf_fds(int *fds, size_t count);
    bool is_connected() const;

private:
    int socket_fd_{-1};
    std::string socket_path_{};
    bool connected_{false};
};

class dmabuf_texture_cache {
public:
    dmabuf_texture_cache() = default;
    ~dmabuf_texture_cache();

    void store(uint32_t slot_index, int fd, uint32_t width, uint32_t height, uint32_t format);
    bool has(uint32_t slot_index) const;
    int get_fd(uint32_t slot_index) const;

private:
    struct cache_entry {
        int fd{-1};
        uint32_t width{0};
        uint32_t height{0};
        uint32_t format{0};
    };

    mutable std::mutex mutex_;
    std::array<cache_entry, 8> entries_{};
    std::array<bool, 8> valid_{};
};

} // namespace nozzle::detail::linux_backend

#endif
