// nozzle - receiver.cpp - Texture receiver implementation

#include <nozzle/receiver.hpp>

#include "backends/backend_dispatch.hpp"
#include "ipc.hpp"
#include "frame_helpers.hpp"
#include "metadata.hpp"
#include "registry.hpp"
#include "shared_state.hpp"

#include <chrono>
#include <cstring>
#include <string>
#include <thread>

namespace nozzle {

namespace {

struct SenderDirectoryEntry {
    char shm_name[96]{};
    char application_name[64]{};
    char uuid[37]{};
    uint8_t backend{0};
    uint64_t pid{0};
};

Result<SenderDirectoryEntry> find_sender_in_directory(const char *name) {
    if (!name || name[0] == '\0') {
        return Error{ErrorCode::InvalidArgument, "sender name must not be empty"};
    }

    auto shm_result = detail::ipc::shm_open_ro(detail::kDirectoryShmName);
    if (!shm_result.ok()) {
        return Error{ErrorCode::SenderNotFound, "no nozzle directory found"};
    }

    const auto total_size = sizeof(detail::DirectoryHeader) +
                            sizeof(detail::DirectoryEntry) * detail::kMaxSenders;

    auto map_result = detail::ipc::shm_map(shm_result.value(), total_size, true);
    if (!map_result.ok()) {
        detail::ipc::shm_close(shm_result.value());
        return Error{ErrorCode::SenderNotFound, "failed to map nozzle directory"};
    }
    void *mapped = map_result.value();

    auto *header = static_cast<const detail::DirectoryHeader *>(mapped);
    if (header->magic != detail::kDirectoryMagic) {
        detail::ipc::shm_unmap(mapped, total_size);
        detail::ipc::shm_close(shm_result.value());
        return Error{ErrorCode::SenderNotFound, "nozzle directory has invalid magic"};
    }

    auto *entries = reinterpret_cast<const detail::DirectoryEntry *>(
        static_cast<const uint8_t *>(mapped) + sizeof(detail::DirectoryHeader));

    SenderDirectoryEntry found{};
    bool found_entry = false;

    for (uint32_t i = 0; i < header->capacity; ++i) {
        if (entries[i].valid != 1) {
            continue;
        }
        if (entries[i].pid == 0) {
            continue;
        }
        if (!detail::ipc::is_pid_alive(entries[i].pid)) {
            continue;
        }
        if (std::strncmp(entries[i].sender_name, name, sizeof(entries[i].sender_name)) != 0) {
            continue;
        }

        std::memcpy(found.shm_name, entries[i].shm_name, sizeof(found.shm_name));
        std::memcpy(found.application_name, entries[i].application_name, sizeof(found.application_name));
        std::memcpy(found.uuid, entries[i].uuid, sizeof(found.uuid));
        found.backend = entries[i].backend;
        found.pid = entries[i].pid;
        found_entry = true;
        break;
    }

    detail::ipc::shm_unmap(mapped, total_size);
    detail::ipc::shm_close(shm_result.value());

    if (!found_entry) {
        return Error{ErrorCode::SenderNotFound, "sender not found: " + std::string(name)};
    }

    return found;
}

struct ConnectionSetup {
    detail::registry::SenderStateView state_view{};
    connected_sender_info info{};
    uint64_t sender_pid{0};
};

Result<ConnectionSetup> establish_connection(const std::string &sender_name) {
    auto dir_entry = find_sender_in_directory(sender_name.c_str());
    if (!dir_entry.ok()) {
        return dir_entry.error();
    }

    auto state_result = detail::registry::open_sender_state(dir_entry.value().shm_name);
    if (!state_result.ok()) {
        return Error{ErrorCode::SenderNotFound,
                     "failed to open sender state for: " + sender_name};
    }

    ConnectionSetup setup{};
    setup.state_view = std::move(state_result.value());
    setup.sender_pid = dir_entry.value().pid;

    auto *state = setup.state_view.state;
    setup.info.name = state->name;
    setup.info.application_name = state->application_name;
    setup.info.id = state->uuid;
    setup.info.backend = static_cast<backend_type>(state->backend);
    setup.info.width = state->width;
    setup.info.height = state->height;
    setup.info.format = static_cast<texture_format>(state->format);
    setup.info.frame_counter = detail::ipc::atomic_load_relaxed(&state->committed_frame);
    detail::ipc::atomic_fence_acquire();
    setup.info.last_update_time_ns = detail::ipc::monotonic_ns();

    return setup;
}

Result<texture> create_texture_from_slot(
    const detail::SenderSharedState *state,
    uint32_t slot) {
    auto backend = static_cast<backend_type>(state->backend);
    if (detail::backend::get_backend_type() != backend) {
        return Error{ErrorCode::BackendError,
            "sender uses a different backend than receiver"};
    }
    return detail::backend::lookup_texture(
        nullptr,
        state->slots[slot].shared_resource_id,
        state->width,
        state->height,
        state->format);
}

} // anonymous namespace

struct receiver::Impl {
    std::string sender_name_{};
    detail::registry::SenderStateView state_view_{};
    uint64_t last_frame_{0};
    uint64_t last_change_counter_{0};
    connected_sender_info connected_info_{};
    bool connected_{false};
    uint64_t sender_pid_{0};
};

receiver::receiver() = default;
receiver::~receiver() {
    if (impl_ && impl_->state_view_.state) {
        detail::registry::close_sender_state(impl_->state_view_);
    }
}

receiver::receiver(receiver &&other) noexcept
    : impl_{std::move(other.impl_)} {}

receiver &receiver::operator=(receiver &&other) noexcept {
    if (this != &other) {
        if (impl_ && impl_->state_view_.state) {
            detail::registry::close_sender_state(impl_->state_view_);
        }
        impl_ = std::move(other.impl_);
    }
    return *this;
}

Result<receiver> receiver::create(const receiver_desc &desc) {
    if (desc.name.empty()) {
        return Error{ErrorCode::InvalidArgument, "sender name must not be empty"};
    }

    auto setup = establish_connection(desc.name);
    if (!setup.ok()) {
        return setup.error();
    }

    receiver recv;
    recv.impl_ = std::make_unique<Impl>();
    recv.impl_->sender_name_ = desc.name;
    recv.impl_->state_view_ = std::move(setup.value().state_view);
    recv.impl_->sender_pid_ = setup.value().sender_pid;
    recv.impl_->connected_ = true;
    recv.impl_->last_frame_ = 0;
    recv.impl_->connected_info_ = std::move(setup.value().info);

    return recv;
}

Result<frame> receiver::acquire_frame() {
    return acquire_frame(acquire_desc{});
}

Result<frame> receiver::acquire_frame(const acquire_desc &desc) {
    if (!impl_ || !impl_->connected_) {
        if (impl_) {
            auto setup = establish_connection(impl_->sender_name_);
            if (!setup.ok()) {
                return Error{ErrorCode::SenderNotFound,
                             "reconnect failed: " + setup.error().message};
            }
            if (impl_->state_view_.state) {
                detail::registry::close_sender_state(impl_->state_view_);
            }
            impl_->state_view_ = std::move(setup.value().state_view);
            impl_->sender_pid_ = setup.value().sender_pid;
            impl_->connected_ = true;
            impl_->last_frame_ = 0;
            impl_->connected_info_ = std::move(setup.value().info);
        } else {
            return Error{ErrorCode::SenderNotFound, "receiver not initialized"};
        }
    }

    if (impl_->sender_pid_ > 0 && !detail::ipc::is_pid_alive(impl_->sender_pid_)) {
        impl_->connected_ = false;
        if (impl_->state_view_.state) {
            detail::registry::close_sender_state(impl_->state_view_);
        }
        return Error{ErrorCode::SenderClosed, "sender process is no longer alive"};
    }

    auto *state = impl_->state_view_.state;
    if (!state) {
        impl_->connected_ = false;
        return Error{ErrorCode::SenderClosed, "sender state is null"};
    }

    uint64_t deadline = 0;
    if (desc.timeout_ms > 0) {
        deadline = detail::ipc::monotonic_ns() + desc.timeout_ms * 1000000ULL;
    }

    while (true) {
        uint64_t frame = detail::ipc::atomic_load_relaxed(&state->committed_frame);
        detail::ipc::atomic_fence_acquire();
        uint32_t slot = detail::ipc::atomic_load_relaxed(&state->committed_slot);

        if (frame == 0) {
            if (desc.timeout_ms > 0) {
                if (detail::ipc::monotonic_ns() >= deadline) {
                    return Error{ErrorCode::Timeout, "timeout waiting for first frame"};
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }
            return Error{ErrorCode::Timeout, "sender has not produced any frames yet"};
        }

        if (frame == impl_->last_frame_) {
            if (desc.timeout_ms > 0) {
                if (detail::ipc::monotonic_ns() >= deadline) {
                    return Error{ErrorCode::Timeout, "timeout waiting for new frame"};
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }
            return Error{ErrorCode::Timeout, "no new frame"};
        }

        if (slot >= state->ring_size) {
            slot = slot % state->ring_size;
        }

        uint32_t dropped = 0;
        if (frame > impl_->last_frame_) {
            dropped = static_cast<uint32_t>(frame - impl_->last_frame_ - 1);
        }
        impl_->last_frame_ = frame;

        frame_info info{};
        info.frame_index = frame;
        info.timestamp_ns = detail::ipc::monotonic_ns();
        info.width = state->width;
        info.height = state->height;
        info.format = static_cast<texture_format>(state->format);
        info.transfer_mode_val = transfer_mode::zero_copy_shared_texture;
        info.sync_mode_val = sync_mode::none;
        info.dropped_frame_count = dropped;

        auto tex_result = create_texture_from_slot(state, slot);
        if (!tex_result.ok()) {
            return tex_result.error();
        }

        impl_->connected_info_.frame_counter = frame;
        impl_->connected_info_.last_update_time_ns = info.timestamp_ns;

        return detail::make_frame(std::move(tex_result.value()), info);
    }
}

connected_sender_info receiver::connected_info() const {
    if (!impl_) {
        return {};
    }
    return impl_->connected_info_;
}

metadata_list receiver::sender_metadata() const {
    if (!impl_ || !impl_->state_view_.state) {
        return {};
    }
    return detail::parse_metadata(
        impl_->state_view_.state->metadata,
        sizeof(impl_->state_view_.state->metadata));
}

bool receiver::is_connected() const {
    return impl_ && impl_->connected_;
}

bool receiver::valid() const {
    return impl_ && impl_->connected_;
}

} // namespace nozzle
