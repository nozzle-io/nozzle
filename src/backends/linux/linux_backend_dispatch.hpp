// nozzle - linux_backend_dispatch.hpp - DMA-BUF backend dispatch implementation

#pragma once

#include "linux_helpers.hpp"
#include "linux_fd_transfer.hpp"
#include <nozzle/backends/linux.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <mutex>

namespace nozzle {
namespace detail {
namespace backend {

namespace {

struct linux_sender_state {
    linux_backend::fd_sender fd_sender_{};
    std::array<int, 8> slot_fds_{};
    uint32_t ring_size_{0};
    bool started_{false};
    std::mutex mutex_;
};

std::atomic<linux_sender_state *> g_sender_state{nullptr};

struct linux_receiver_cache {
    linux_backend::dmabuf_texture_cache cache_{};
    std::mutex mutex_;
};

linux_receiver_cache &get_receiver_cache() {
    static linux_receiver_cache instance;
    return instance;
}

} // anonymous namespace

inline auto get_default_device() -> void * {
    return linux_backend::get_default_gbm_device();
}

inline auto create_ring_texture(
    void *device, uint32_t width, uint32_t height, uint32_t format
) -> Result<texture> {
    auto tex_result = linux_backend::create_dmabuf_texture(device, width, height, format);
    if (!tex_result.ok()) {
        return tex_result.error();
    }

    auto *state = g_sender_state.load(std::memory_order_relaxed);
    if (!state) {
        auto *new_state = new linux_sender_state{};
        linux_sender_state *expected = nullptr;
        if (!g_sender_state.compare_exchange_strong(expected, new_state,
                std::memory_order_release, std::memory_order_relaxed)) {
            delete new_state;
            state = expected;
        } else {
            state = new_state;
        }
    }

    if (state) {
        std::lock_guard<std::mutex> lock(state->mutex_);
        void *surface = detail::get_surface_native(tex_result.value());
        int fd = static_cast<int>(reinterpret_cast<intptr_t>(surface));
        uint32_t slot_index = 0;
        bool found_existing = false;

        for (uint32_t i = 0; i < 8; ++i) {
            if (state->slot_fds_[i] == fd) {
                slot_index = i;
                found_existing = true;
                break;
            }
        }

        if (!found_existing) {
            for (uint32_t i = 0; i < 8; ++i) {
                if (state->slot_fds_[i] == 0) {
                    slot_index = i;
                    break;
                }
            }
            state->slot_fds_[slot_index] = fd;
        }
        state->ring_size_ = std::max(state->ring_size_, slot_index + 1u);

        if (!state->started_) {
            // TODO: Phase 1 uses fixed socket name. Phase 2: pass sender UUID
            // through backend dispatch to support multiple concurrent senders.
            state->fd_sender_.start("nozzle_default");
            state->started_ = true;
        }
    }

    return tex_result;
}

inline auto get_shared_resource_id(const texture &tex) -> uint64_t {
    void *surface = detail::get_surface_native(tex);
    if (!surface) {
        return 0;
    }
    int fd = static_cast<int>(reinterpret_cast<intptr_t>(surface));

    auto *state = g_sender_state.load(std::memory_order_relaxed);
    if (state) {
        std::lock_guard<std::mutex> lock(state->mutex_);
        for (uint32_t i = 0; i < 8; ++i) {
            if (state->slot_fds_[i] == fd) {
                return static_cast<uint64_t>(i);
            }
        }
    }

    return 0;
}

inline auto get_native_surface(const texture &tex) -> void * {
    return detail::get_surface_native(tex);
}

inline void release_texture_resources(void *native_texture, void *native_surface) {
    void *egl_disp = linux_backend::get_egl_display();
    if (native_texture && egl_disp) {
        linux_backend::destroy_egl_image(egl_disp, native_texture);
    }
    if (native_surface) {
        int fd = static_cast<int>(reinterpret_cast<intptr_t>(native_surface));
        if (fd >= 0) {
            linux_backend::close_drm_fd(fd);
        }
    }
}

inline auto lookup_texture(
    void * /*device*/, uint64_t shared_id, uint32_t width, uint32_t height, uint32_t format
) -> Result<texture> {
    uint32_t slot_index = static_cast<uint32_t>(shared_id);
    if (slot_index >= 8) {
        return Error{ErrorCode::InvalidArgument, "DMA-BUF slot index out of range"};
    }

    auto &cache = get_receiver_cache();
    std::lock_guard<std::mutex> lock(cache.mutex_);

    if (cache.cache_.has(slot_index)) {
        int fd = cache.cache_.get_fd(slot_index);
        uint32_t fourcc = linux_backend::drm_format_from_nozzle(format);
        void *egl_disp = linux_backend::get_egl_display();
        if (!egl_disp) {
            return Error{ErrorCode::BackendError, "No EGL display available"};
        }

        void *egl_image = linux_backend::import_egl_image(
            egl_disp, fd, width, height, fourcc, 0, 0);
        if (!egl_image) {
            return Error{ErrorCode::ResourceCreationFailed,
                "Failed to import DMA-BUF fd as EGLImage"};
        }

        void *native_surface = reinterpret_cast<void *>(static_cast<intptr_t>(fd));
        return make_texture_from_backend(egl_image, native_surface, width, height, format);
    }

    return Error{ErrorCode::BackendError,
        "DMA-BUF slot not cached — fd transfer requires sender UUID"};
}

inline auto wrap_backend_texture(
    void *backend_texture, void *backend_surface,
    uint32_t width, uint32_t height, uint32_t pixel_format
) -> texture {
    return make_texture_from_backend(backend_texture, backend_surface, width, height, pixel_format);
}

inline auto get_backend_type() -> backend_type {
    return backend_type::dma_buf;
}

} // namespace backend
} // namespace detail
} // namespace nozzle
