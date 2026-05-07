#pragma once

#if NOZZLE_HAS_DMA_BUF

#include <array>
#include <cstdint>
#include <mutex>
#include <string>

namespace nozzle::detail::linux_backend {

bool send_fd_response(int socket_fd, int fd_to_send);
int recv_fd_response(int socket_fd);

class dmabuf_texture_cache {
public:
    dmabuf_texture_cache() = default;
    ~dmabuf_texture_cache();

    void store(uint32_t slot_index, int fd, uint32_t width, uint32_t height, uint32_t format,
               uint32_t plane_count = 0, const uint32_t *plane_strides = nullptr, const uint32_t *plane_offsets = nullptr);
    bool has(uint32_t slot_index) const;
    int get_fd(uint32_t slot_index) const;
    uint32_t get_plane_count(uint32_t slot_index) const;
    void get_plane_metadata(uint32_t slot_index, uint32_t &plane_count, uint32_t (&strides)[4], uint32_t (&offsets)[4]) const;

private:
    struct cache_entry {
        int fd{-1};
        uint32_t width{0};
        uint32_t height{0};
        uint32_t format{0};
        uint32_t plane_count{0};
        uint32_t plane_strides[4]{};
        uint32_t plane_offsets[4]{};
    };

    mutable std::mutex mutex_;
    std::array<cache_entry, 8> entries_{};
    std::array<bool, 8> valid_{};
};

} // namespace nozzle::detail::linux_backend

#endif
