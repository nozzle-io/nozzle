// nozzle - nozzle_c.cpp - C ABI wrapper for nozzle public API

#include <bbb/nozzle/nozzle_c.h>

#include <bbb/nozzle/sender.hpp>
#include <bbb/nozzle/receiver.hpp>
#include <bbb/nozzle/frame.hpp>
#include <bbb/nozzle/texture.hpp>
#include <bbb/nozzle/device.hpp>
#include <bbb/nozzle/discovery.hpp>
#include <bbb/nozzle/pixel_access.hpp>

#if NOZZLE_HAS_OPENGL
#include <bbb/nozzle/backends/opengl.hpp>
#endif

#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>

namespace {

NozzleErrorCode to_c_error(bbb::nozzle::ErrorCode code) {
    switch (code) {
        case bbb::nozzle::ErrorCode::Ok: return NOZZLE_OK;
        case bbb::nozzle::ErrorCode::Unknown: return NOZZLE_ERROR_UNKNOWN;
        case bbb::nozzle::ErrorCode::InvalidArgument: return NOZZLE_ERROR_INVALID_ARGUMENT;
        case bbb::nozzle::ErrorCode::UnsupportedBackend: return NOZZLE_ERROR_UNSUPPORTED_BACKEND;
        case bbb::nozzle::ErrorCode::UnsupportedFormat: return NOZZLE_ERROR_UNSUPPORTED_FORMAT;
        case bbb::nozzle::ErrorCode::DeviceMismatch: return NOZZLE_ERROR_DEVICE_MISMATCH;
        case bbb::nozzle::ErrorCode::ResourceCreationFailed: return NOZZLE_ERROR_RESOURCE_CREATION_FAILED;
        case bbb::nozzle::ErrorCode::SharedHandleFailed: return NOZZLE_ERROR_SHARED_HANDLE_FAILED;
        case bbb::nozzle::ErrorCode::SenderNotFound: return NOZZLE_ERROR_SENDER_NOT_FOUND;
        case bbb::nozzle::ErrorCode::SenderClosed: return NOZZLE_ERROR_SENDER_CLOSED;
        case bbb::nozzle::ErrorCode::Timeout: return NOZZLE_ERROR_TIMEOUT;
        case bbb::nozzle::ErrorCode::BackendError: return NOZZLE_ERROR_BACKEND_ERROR;
    }
    return NOZZLE_ERROR_UNKNOWN;
}

NozzleBackendType to_c_backend_type(bbb::nozzle::backend_type bt) {
    switch (bt) {
        case bbb::nozzle::backend_type::d3d11: return NOZZLE_BACKEND_D3D11;
        case bbb::nozzle::backend_type::metal: return NOZZLE_BACKEND_METAL;
        case bbb::nozzle::backend_type::opengl: return NOZZLE_BACKEND_OPENGL;
        case bbb::nozzle::backend_type::unknown:
        default: return NOZZLE_BACKEND_UNKNOWN;
    }
}

NozzleTextureFormat to_c_format(bbb::nozzle::texture_format fmt) {
    switch (fmt) {
        case bbb::nozzle::texture_format::r8_unorm: return NOZZLE_FORMAT_R8_UNORM;
        case bbb::nozzle::texture_format::rg8_unorm: return NOZZLE_FORMAT_RG8_UNORM;
        case bbb::nozzle::texture_format::rgba8_unorm: return NOZZLE_FORMAT_RGBA8_UNORM;
        case bbb::nozzle::texture_format::bgra8_unorm: return NOZZLE_FORMAT_BGRA8_UNORM;
        case bbb::nozzle::texture_format::rgba8_srgb: return NOZZLE_FORMAT_RGBA8_SRGB;
        case bbb::nozzle::texture_format::bgra8_srgb: return NOZZLE_FORMAT_BGRA8_SRGB;
        case bbb::nozzle::texture_format::r16_unorm: return NOZZLE_FORMAT_R16_UNORM;
        case bbb::nozzle::texture_format::rg16_unorm: return NOZZLE_FORMAT_RG16_UNORM;
        case bbb::nozzle::texture_format::rgba16_unorm: return NOZZLE_FORMAT_RGBA16_UNORM;
        case bbb::nozzle::texture_format::r16_float: return NOZZLE_FORMAT_R16_FLOAT;
        case bbb::nozzle::texture_format::rg16_float: return NOZZLE_FORMAT_RG16_FLOAT;
        case bbb::nozzle::texture_format::rgba16_float: return NOZZLE_FORMAT_RGBA16_FLOAT;
        case bbb::nozzle::texture_format::r32_float: return NOZZLE_FORMAT_R32_FLOAT;
        case bbb::nozzle::texture_format::rg32_float: return NOZZLE_FORMAT_RG32_FLOAT;
        case bbb::nozzle::texture_format::rgba32_float: return NOZZLE_FORMAT_RGBA32_FLOAT;
        case bbb::nozzle::texture_format::r32_uint: return NOZZLE_FORMAT_R32_UINT;
        case bbb::nozzle::texture_format::rgba32_uint: return NOZZLE_FORMAT_RGBA32_UINT;
        case bbb::nozzle::texture_format::depth32_float: return NOZZLE_FORMAT_DEPTH32_FLOAT;
        case bbb::nozzle::texture_format::unknown:
        default: return NOZZLE_FORMAT_UNKNOWN;
    }
}

bbb::nozzle::texture_format to_cpp_format(NozzleTextureFormat fmt) {
    switch (fmt) {
        case NOZZLE_FORMAT_R8_UNORM: return bbb::nozzle::texture_format::r8_unorm;
        case NOZZLE_FORMAT_RG8_UNORM: return bbb::nozzle::texture_format::rg8_unorm;
        case NOZZLE_FORMAT_RGBA8_UNORM: return bbb::nozzle::texture_format::rgba8_unorm;
        case NOZZLE_FORMAT_BGRA8_UNORM: return bbb::nozzle::texture_format::bgra8_unorm;
        case NOZZLE_FORMAT_RGBA8_SRGB: return bbb::nozzle::texture_format::rgba8_srgb;
        case NOZZLE_FORMAT_BGRA8_SRGB: return bbb::nozzle::texture_format::bgra8_srgb;
        case NOZZLE_FORMAT_R16_UNORM: return bbb::nozzle::texture_format::r16_unorm;
        case NOZZLE_FORMAT_RG16_UNORM: return bbb::nozzle::texture_format::rg16_unorm;
        case NOZZLE_FORMAT_RGBA16_UNORM: return bbb::nozzle::texture_format::rgba16_unorm;
        case NOZZLE_FORMAT_R16_FLOAT: return bbb::nozzle::texture_format::r16_float;
        case NOZZLE_FORMAT_RG16_FLOAT: return bbb::nozzle::texture_format::rg16_float;
        case NOZZLE_FORMAT_RGBA16_FLOAT: return bbb::nozzle::texture_format::rgba16_float;
        case NOZZLE_FORMAT_R32_FLOAT: return bbb::nozzle::texture_format::r32_float;
        case NOZZLE_FORMAT_RG32_FLOAT: return bbb::nozzle::texture_format::rg32_float;
        case NOZZLE_FORMAT_RGBA32_FLOAT: return bbb::nozzle::texture_format::rgba32_float;
        case NOZZLE_FORMAT_R32_UINT: return bbb::nozzle::texture_format::r32_uint;
        case NOZZLE_FORMAT_RGBA32_UINT: return bbb::nozzle::texture_format::rgba32_uint;
        case NOZZLE_FORMAT_DEPTH32_FLOAT: return bbb::nozzle::texture_format::depth32_float;
        default: return bbb::nozzle::texture_format::unknown;
    }
}

bbb::nozzle::receive_mode to_cpp_receive_mode(NozzleReceiveMode mode) {
    switch (mode) {
        case NOZZLE_RECEIVE_SEQUENTIAL_BEST_EFFORT:
            return bbb::nozzle::receive_mode::sequential_best_effort;
        case NOZZLE_RECEIVE_LATEST_ONLY:
        default:
            return bbb::nozzle::receive_mode::latest_only;
    }
}

} // anonymous namespace

struct NozzleSender {
    std::unique_ptr<bbb::nozzle::sender> obj;
    bbb::nozzle::sender_info cached_info{};
};

struct NozzleReceiver {
    std::unique_ptr<bbb::nozzle::receiver> obj;
    bbb::nozzle::connected_sender_info cached_connected_info{};
};

struct NozzleFrame {
    std::unique_ptr<bbb::nozzle::frame> obj;
    std::unique_ptr<bbb::nozzle::writable_frame> writable;
    bool is_writable{false};
};

struct NozzleTexture {
    std::unique_ptr<bbb::nozzle::texture> obj;
};

struct NozzleDevice {
    std::unique_ptr<bbb::nozzle::device> obj;
};

// ========== Sender API ==========

extern "C" {

NozzleErrorCode nozzle_sender_create(
    const NozzleSenderDesc *desc,
    NozzleSender **out_sender
) {
    if (!desc || !out_sender) return NOZZLE_ERROR_INVALID_ARGUMENT;

    bbb::nozzle::sender_desc cpp_desc{};
    if (desc->name) cpp_desc.name = desc->name;
    if (desc->application_name) cpp_desc.application_name = desc->application_name;
    cpp_desc.ring_buffer_size = desc->ring_buffer_size;
    cpp_desc.allow_format_fallback = desc->allow_format_fallback != 0;

    auto result = bbb::nozzle::sender::create(cpp_desc);
    if (!result.ok()) return to_c_error(result.error().code);

    auto *wrapper = new (std::nothrow) NozzleSender{};
    if (!wrapper) return NOZZLE_ERROR_RESOURCE_CREATION_FAILED;

    wrapper->obj = std::make_unique<bbb::nozzle::sender>(std::move(result.value()));
    *out_sender = wrapper;
    return NOZZLE_OK;
}

void nozzle_sender_destroy(NozzleSender *sender) {
    delete sender;
}

NozzleErrorCode nozzle_sender_publish_texture(
    NozzleSender *sender,
    NozzleTexture *texture
) {
    if (!sender || !texture) return NOZZLE_ERROR_INVALID_ARGUMENT;

    auto result = sender->obj->publish_external_texture(*texture->obj);
    if (!result.ok()) return to_c_error(result.error().code);
    return NOZZLE_OK;
}

NozzleErrorCode nozzle_sender_acquire_writable_frame(
    NozzleSender *sender,
    uint32_t width,
    uint32_t height,
    NozzleTextureFormat format,
    NozzleFrame **out_frame
) {
    if (!sender || !out_frame) return NOZZLE_ERROR_INVALID_ARGUMENT;

    bbb::nozzle::texture_desc td{};
    td.width = width;
    td.height = height;
    td.format = to_cpp_format(format);

    auto result = sender->obj->acquire_writable_frame(td);
    if (!result.ok()) return to_c_error(result.error().code);

    auto *wrapper = new (std::nothrow) NozzleFrame{};
    if (!wrapper) return NOZZLE_ERROR_RESOURCE_CREATION_FAILED;

    wrapper->writable = std::make_unique<bbb::nozzle::writable_frame>(std::move(result.value()));
    wrapper->is_writable = true;
    *out_frame = wrapper;
    return NOZZLE_OK;
}

NozzleErrorCode nozzle_sender_commit_frame(
    NozzleSender *sender,
    NozzleFrame *frame
) {
    if (!sender || !frame || !frame->is_writable) return NOZZLE_ERROR_INVALID_ARGUMENT;

    auto result = sender->obj->commit_frame(*frame->writable);
    if (!result.ok()) return to_c_error(result.error().code);
    return NOZZLE_OK;
}

NozzleErrorCode nozzle_sender_get_info(
    NozzleSender *sender,
    NozzleSenderInfo *out_info
) {
    if (!sender || !out_info) return NOZZLE_ERROR_INVALID_ARGUMENT;

    sender->cached_info = sender->obj->info();
    out_info->name = sender->cached_info.name.c_str();
    out_info->application_name = sender->cached_info.application_name.c_str();
    out_info->id = sender->cached_info.id.c_str();
    out_info->backend = to_c_backend_type(sender->cached_info.backend);
    return NOZZLE_OK;
}

// ========== Receiver API ==========

NozzleErrorCode nozzle_receiver_create(
    const NozzleReceiverDesc *desc,
    NozzleReceiver **out_receiver
) {
    if (!desc || !out_receiver) return NOZZLE_ERROR_INVALID_ARGUMENT;

    bbb::nozzle::receiver_desc cpp_desc{};
    if (desc->name) cpp_desc.name = desc->name;
    if (desc->application_name) cpp_desc.application_name = desc->application_name;
    cpp_desc.receive_mode_val = to_cpp_receive_mode(desc->receive_mode);

    auto result = bbb::nozzle::receiver::create(cpp_desc);
    if (!result.ok()) return to_c_error(result.error().code);

    auto *wrapper = new (std::nothrow) NozzleReceiver{};
    if (!wrapper) return NOZZLE_ERROR_RESOURCE_CREATION_FAILED;

    wrapper->obj = std::make_unique<bbb::nozzle::receiver>(std::move(result.value()));
    *out_receiver = wrapper;
    return NOZZLE_OK;
}

void nozzle_receiver_destroy(NozzleReceiver *receiver) {
    delete receiver;
}

NozzleErrorCode nozzle_receiver_acquire_frame(
    NozzleReceiver *receiver,
    const NozzleAcquireDesc *desc,
    NozzleFrame **out_frame
) {
    if (!receiver || !out_frame) return NOZZLE_ERROR_INVALID_ARGUMENT;

    auto make_frame_wrapper = [](bbb::nozzle::frame &&f) -> NozzleFrame * {
        auto *w = new (std::nothrow) NozzleFrame{};
        if (!w) return nullptr;
        w->obj = std::make_unique<bbb::nozzle::frame>(std::move(f));
        w->is_writable = false;
        return w;
    };

    NozzleFrame *wrapper = nullptr;

    if (desc) {
        bbb::nozzle::acquire_desc ad{};
        ad.timeout_ms = desc->timeout_ms;
        auto result = receiver->obj->acquire_frame(ad);
        if (!result.ok()) return to_c_error(result.error().code);
        wrapper = make_frame_wrapper(std::move(result.value()));
    } else {
        auto result = receiver->obj->acquire_frame();
        if (!result.ok()) return to_c_error(result.error().code);
        wrapper = make_frame_wrapper(std::move(result.value()));
    }

    if (!wrapper) return NOZZLE_ERROR_RESOURCE_CREATION_FAILED;
    *out_frame = wrapper;
    return NOZZLE_OK;
}

void nozzle_frame_release(NozzleFrame *frame) {
    delete frame;
}

NozzleErrorCode nozzle_receiver_get_connected_info(
    NozzleReceiver *receiver,
    NozzleConnectedSenderInfo *out_info
) {
    if (!receiver || !out_info) return NOZZLE_ERROR_INVALID_ARGUMENT;

    receiver->cached_connected_info = receiver->obj->connected_info();
    const auto &ci = receiver->cached_connected_info;
    out_info->name = ci.name.c_str();
    out_info->application_name = ci.application_name.c_str();
    out_info->id = ci.id.c_str();
    out_info->backend = to_c_backend_type(ci.backend);
    out_info->width = ci.width;
    out_info->height = ci.height;
    out_info->format = to_c_format(ci.format);
    out_info->estimated_fps = ci.estimated_fps;
    out_info->frame_counter = ci.frame_counter;
    out_info->last_update_time_ns = ci.last_update_time_ns;
    return NOZZLE_OK;
}

NozzleErrorCode nozzle_frame_get_info(
    NozzleFrame *frame,
    NozzleFrameInfo *out_info
) {
    if (!frame || !out_info) return NOZZLE_ERROR_INVALID_ARGUMENT;

    if (frame->is_writable) {
        const auto &td = frame->writable->desc();
        out_info->frame_index = 0;
        out_info->timestamp_ns = 0;
        out_info->width = td.width;
        out_info->height = td.height;
        out_info->format = to_c_format(td.format);
        out_info->dropped_frame_count = 0;
    } else {
        const auto fi = frame->obj->info();
        out_info->frame_index = fi.frame_index;
        out_info->timestamp_ns = fi.timestamp_ns;
        out_info->width = fi.width;
        out_info->height = fi.height;
        out_info->format = to_c_format(fi.format);
        out_info->dropped_frame_count = fi.dropped_frame_count;
    }
    return NOZZLE_OK;
}

// ========== Discovery ==========

NozzleErrorCode nozzle_enumerate_senders(
    NozzleSenderInfoArray *out_array
) {
    if (!out_array) return NOZZLE_ERROR_INVALID_ARGUMENT;

    auto senders = bbb::nozzle::enumerate_senders();
    auto count = static_cast<uint32_t>(senders.size());

    if (count == 0) {
        out_array->items = nullptr;
        out_array->count = 0;
        return NOZZLE_OK;
    }

    std::size_t string_storage = 0;
    for (const auto &s : senders) {
        string_storage += s.name.size() + 1;
        string_storage += s.application_name.size() + 1;
        string_storage += s.id.size() + 1;
    }

    std::size_t infos_size = count * sizeof(NozzleSenderInfo);
    std::size_t total = infos_size + string_storage;

    void *buf = std::malloc(total);
    if (!buf) return NOZZLE_ERROR_RESOURCE_CREATION_FAILED;

    auto *infos = static_cast<NozzleSenderInfo *>(buf);
    char *str_ptr = static_cast<char *>(buf) + infos_size;

    for (uint32_t i = 0; i < count; ++i) {
        const auto &s = senders[i];
        infos[i].backend = to_c_backend_type(s.backend);

        std::memcpy(str_ptr, s.name.c_str(), s.name.size() + 1);
        infos[i].name = str_ptr;
        str_ptr += s.name.size() + 1;

        std::memcpy(str_ptr, s.application_name.c_str(), s.application_name.size() + 1);
        infos[i].application_name = str_ptr;
        str_ptr += s.application_name.size() + 1;

        std::memcpy(str_ptr, s.id.c_str(), s.id.size() + 1);
        infos[i].id = str_ptr;
        str_ptr += s.id.size() + 1;
    }

    out_array->items = infos;
    out_array->count = count;
    return NOZZLE_OK;
}

void nozzle_free_sender_info_array(NozzleSenderInfoArray *array) {
    if (!array) return;
    std::free(array->items);
    array->items = nullptr;
    array->count = 0;
}

// ========== Device API ==========

NozzleErrorCode nozzle_device_get_default(
    NozzleDevice **out_device
) {
    if (!out_device) return NOZZLE_ERROR_INVALID_ARGUMENT;

    auto result = bbb::nozzle::device::default_device();
    if (!result.ok()) return to_c_error(result.error().code);

    auto *wrapper = new (std::nothrow) NozzleDevice{};
    if (!wrapper) return NOZZLE_ERROR_RESOURCE_CREATION_FAILED;

    wrapper->obj = std::make_unique<bbb::nozzle::device>(std::move(result.value()));
    *out_device = wrapper;
    return NOZZLE_OK;
}

void nozzle_device_destroy(NozzleDevice *device) {
    delete device;
}

// ========== Pixel Access (CPU) ==========

NozzleErrorCode nozzle_frame_lock_pixels(
    NozzleFrame *frame,
    NozzleMappedPixels *out_pixels
) {
    if (!frame || !out_pixels || !frame->obj) return NOZZLE_ERROR_INVALID_ARGUMENT;

    auto result = bbb::nozzle::lock_frame_pixels(*frame->obj);
    if (!result.ok()) return to_c_error(result.error().code);

    const auto &mp = result.value();
    out_pixels->data = mp.data;
    out_pixels->row_bytes = mp.row_bytes;
    out_pixels->width = mp.width;
    out_pixels->height = mp.height;
    out_pixels->format = to_c_format(mp.format);
    return NOZZLE_OK;
}

void nozzle_frame_unlock_pixels(NozzleFrame *frame) {
    if (!frame || !frame->obj) return;
    bbb::nozzle::unlock_frame_pixels(*frame->obj);
}

NozzleErrorCode nozzle_frame_lock_writable_pixels(
    NozzleFrame *frame,
    NozzleMappedPixels *out_pixels
) {
    if (!frame || !out_pixels || !frame->writable) return NOZZLE_ERROR_INVALID_ARGUMENT;

    auto result = bbb::nozzle::lock_writable_pixels(*frame->writable);
    if (!result.ok()) return to_c_error(result.error().code);

    const auto &mp = result.value();
    out_pixels->data = mp.data;
    out_pixels->row_bytes = mp.row_bytes;
    out_pixels->width = mp.width;
    out_pixels->height = mp.height;
    out_pixels->format = to_c_format(mp.format);
    return NOZZLE_OK;
}

void nozzle_frame_unlock_writable_pixels(NozzleFrame *frame) {
    if (!frame || !frame->writable) return;
    bbb::nozzle::unlock_writable_pixels(*frame->writable);
}

// ========== GL Interop ==========

NozzleErrorCode nozzle_sender_publish_gl_texture(
    NozzleSender *sender,
    uint32_t gl_texture_name,
    uint32_t gl_target,
    uint32_t width,
    uint32_t height,
    NozzleTextureFormat format
) {
#if NOZZLE_HAS_OPENGL
    if (!sender) return NOZZLE_ERROR_INVALID_ARGUMENT;

    bbb::nozzle::gl::gl_texture_desc gl_desc{};
    gl_desc.name = gl_texture_name;
    gl_desc.target = gl_target;
    gl_desc.width = width;
    gl_desc.height = height;
    gl_desc.format = to_cpp_format(format);

    auto result = bbb::nozzle::gl::publish_gl_texture(*sender->obj, gl_desc);
    if (!result.ok()) return to_c_error(result.error().code);
    return NOZZLE_OK;
#else
    return NOZZLE_ERROR_UNSUPPORTED_BACKEND;
#endif
}

NozzleErrorCode nozzle_frame_copy_to_gl_texture(
    NozzleFrame *frame,
    uint32_t gl_texture_name,
    uint32_t gl_target,
    uint32_t width,
    uint32_t height,
    NozzleTextureFormat format
) {
#if NOZZLE_HAS_OPENGL
    if (!frame || !frame->obj) return NOZZLE_ERROR_INVALID_ARGUMENT;

    bbb::nozzle::gl::gl_texture_desc gl_desc{};
    gl_desc.name = gl_texture_name;
    gl_desc.target = gl_target;
    gl_desc.width = width;
    gl_desc.height = height;
    gl_desc.format = to_cpp_format(format);

    auto result = bbb::nozzle::gl::copy_frame_to_gl_texture(*frame->obj, gl_desc);
    if (!result.ok()) return to_c_error(result.error().code);
    return NOZZLE_OK;
#else
    return NOZZLE_ERROR_UNSUPPORTED_BACKEND;
#endif
}

} // extern "C"
