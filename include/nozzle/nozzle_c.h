#pragma once

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// ========== Opaque Types ==========

typedef struct NozzleSender NozzleSender;
typedef struct NozzleReceiver NozzleReceiver;
typedef struct NozzleFrame NozzleFrame;
typedef struct NozzleTexture NozzleTexture;
typedef struct NozzleDevice NozzleDevice;

// ========== Error Codes ==========

typedef enum NozzleErrorCode {
    NOZZLE_OK = 0,
    NOZZLE_ERROR_UNKNOWN,
    NOZZLE_ERROR_INVALID_ARGUMENT,
    NOZZLE_ERROR_UNSUPPORTED_BACKEND,
    NOZZLE_ERROR_UNSUPPORTED_FORMAT,
    NOZZLE_ERROR_DEVICE_MISMATCH,
    NOZZLE_ERROR_RESOURCE_CREATION_FAILED,
    NOZZLE_ERROR_SHARED_HANDLE_FAILED,
    NOZZLE_ERROR_SENDER_NOT_FOUND,
    NOZZLE_ERROR_SENDER_CLOSED,
    NOZZLE_ERROR_TIMEOUT,
    NOZZLE_ERROR_BACKEND_ERROR,
} NozzleErrorCode;

// ========== Enums ==========

typedef enum NozzleBackendType {
    NOZZLE_BACKEND_UNKNOWN = 0,
    NOZZLE_BACKEND_D3D11,
    NOZZLE_BACKEND_METAL,
    NOZZLE_BACKEND_OPENGL,
} NozzleBackendType;

typedef enum NozzleTextureFormat {
    NOZZLE_FORMAT_UNKNOWN = 0,
    NOZZLE_FORMAT_R8_UNORM,
    NOZZLE_FORMAT_RG8_UNORM,
    NOZZLE_FORMAT_RGBA8_UNORM,
    NOZZLE_FORMAT_BGRA8_UNORM,
    NOZZLE_FORMAT_RGBA8_SRGB,
    NOZZLE_FORMAT_BGRA8_SRGB,
    NOZZLE_FORMAT_R16_UNORM,
    NOZZLE_FORMAT_RG16_UNORM,
    NOZZLE_FORMAT_RGBA16_UNORM,
    NOZZLE_FORMAT_R16_FLOAT,
    NOZZLE_FORMAT_RG16_FLOAT,
    NOZZLE_FORMAT_RGBA16_FLOAT,
    NOZZLE_FORMAT_R32_FLOAT,
    NOZZLE_FORMAT_RG32_FLOAT,
    NOZZLE_FORMAT_RGBA32_FLOAT,
    NOZZLE_FORMAT_R32_UINT,
    NOZZLE_FORMAT_RGBA32_UINT,
    NOZZLE_FORMAT_DEPTH32_FLOAT,
} NozzleTextureFormat;

typedef enum NozzleReceiveMode {
    NOZZLE_RECEIVE_LATEST_ONLY = 0,
    NOZZLE_RECEIVE_SEQUENTIAL_BEST_EFFORT,
} NozzleReceiveMode;

typedef enum NozzleFrameStatus {
    NOZZLE_FRAME_NEW = 0,
    NOZZLE_FRAME_NO_NEW,
    NOZZLE_FRAME_DROPPED,
    NOZZLE_FRAME_SENDER_CLOSED,
    NOZZLE_FRAME_ERROR,
} NozzleFrameStatus;

// ========== Descriptor Structs ==========

typedef struct NozzleSenderDesc {
    const char *name;
    const char *application_name;
    uint32_t ring_buffer_size;
    int allow_format_fallback;
} NozzleSenderDesc;

typedef struct NozzleReceiverDesc {
    const char *name;
    const char *application_name;
    NozzleReceiveMode receive_mode;
} NozzleReceiverDesc;

typedef struct NozzleAcquireDesc {
    uint64_t timeout_ms;
} NozzleAcquireDesc;

// ========== Sender Info ==========

typedef struct NozzleSenderInfo {
    const char *name;
    const char *application_name;
    const char *id;
    NozzleBackendType backend;
} NozzleSenderInfo;

typedef struct NozzleConnectedSenderInfo {
    const char *name;
    const char *application_name;
    const char *id;
    NozzleBackendType backend;
    uint32_t width;
    uint32_t height;
    NozzleTextureFormat format;
    double estimated_fps;
    uint64_t frame_counter;
    uint64_t last_update_time_ns;
} NozzleConnectedSenderInfo;

// ========== Frame Info ==========

typedef struct NozzleFrameInfo {
    uint64_t frame_index;
    uint64_t timestamp_ns;
    uint32_t width;
    uint32_t height;
    NozzleTextureFormat format;
    uint32_t dropped_frame_count;
} NozzleFrameInfo;

// ========== Sender API ==========

NozzleErrorCode nozzle_sender_create(
    const NozzleSenderDesc *desc,
    NozzleSender **out_sender
);

void nozzle_sender_destroy(NozzleSender *sender);

NozzleErrorCode nozzle_sender_publish_texture(
    NozzleSender *sender,
    NozzleTexture *texture
);

NozzleErrorCode nozzle_sender_acquire_writable_frame(
    NozzleSender *sender,
    uint32_t width,
    uint32_t height,
    NozzleTextureFormat format,
    NozzleFrame **out_frame
);

NozzleErrorCode nozzle_sender_commit_frame(
    NozzleSender *sender,
    NozzleFrame *frame
);

NozzleErrorCode nozzle_sender_get_info(
    NozzleSender *sender,
    NozzleSenderInfo *out_info
);

// ========== Receiver API ==========

NozzleErrorCode nozzle_receiver_create(
    const NozzleReceiverDesc *desc,
    NozzleReceiver **out_receiver
);

void nozzle_receiver_destroy(NozzleReceiver *receiver);

NozzleErrorCode nozzle_receiver_acquire_frame(
    NozzleReceiver *receiver,
    const NozzleAcquireDesc *desc,
    NozzleFrame **out_frame
);

void nozzle_frame_release(NozzleFrame *frame);

NozzleErrorCode nozzle_receiver_get_connected_info(
    NozzleReceiver *receiver,
    NozzleConnectedSenderInfo *out_info
);

NozzleErrorCode nozzle_frame_get_info(
    NozzleFrame *frame,
    NozzleFrameInfo *out_info
);

// ========== Discovery ==========

typedef struct NozzleSenderInfoArray {
    NozzleSenderInfo *items;
    uint32_t count;
} NozzleSenderInfoArray;

NozzleErrorCode nozzle_enumerate_senders(
    NozzleSenderInfoArray *out_array
);

void nozzle_free_sender_info_array(NozzleSenderInfoArray *array);

// ========== Device API ==========

NozzleErrorCode nozzle_device_get_default(
    NozzleDevice **out_device
);

void nozzle_device_destroy(NozzleDevice *device);

// ========== Pixel Access (CPU) ==========

typedef struct NozzleMappedPixels {
    void *data;
    uint32_t row_bytes;
    uint32_t width;
    uint32_t height;
    NozzleTextureFormat format;
} NozzleMappedPixels;

NozzleErrorCode nozzle_frame_lock_pixels(
    NozzleFrame *frame,
    NozzleMappedPixels *out_pixels
);

void nozzle_frame_unlock_pixels(NozzleFrame *frame);

NozzleErrorCode nozzle_frame_lock_writable_pixels(
    NozzleFrame *frame,
    NozzleMappedPixels *out_pixels
);

void nozzle_frame_unlock_writable_pixels(NozzleFrame *frame);

// ========== GL Interop ==========

NozzleErrorCode nozzle_sender_publish_gl_texture(
    NozzleSender *sender,
    uint32_t gl_texture_name,
    uint32_t gl_target,
    uint32_t width,
    uint32_t height,
    NozzleTextureFormat format
);

NozzleErrorCode nozzle_frame_copy_to_gl_texture(
    NozzleFrame *frame,
    uint32_t gl_texture_name,
    uint32_t gl_target,
    uint32_t width,
    uint32_t height,
    NozzleTextureFormat format
);

#ifdef __cplusplus
} // extern "C"
#endif
