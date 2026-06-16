// nozzle - opengl_backend.cpp - OpenGL interop
// macOS: IOSurface blit
// Windows: WGL_NV_DX_interop2 GPU path with explicit CPU fallback

#include <nozzle/backends/opengl.hpp>
#include <nozzle/sender.hpp>
#include <nozzle/frame.hpp>
#include <nozzle/texture.hpp>
#include <nozzle/result.hpp>

#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#if NOZZLE_PLATFORM_MACOS
#include <nozzle/backends/metal.hpp>
#include "../metal/metal_helpers.hpp"
#define GL_SILENCE_DEPRECATION
#include <OpenGL/gl.h>
#include <OpenGL/OpenGL.h>
#include <IOSurface/IOSurface.h>
#elif NOZZLE_PLATFORM_WINDOWS
#include <nozzle/backends/d3d11.hpp>
#include "../d3d11/d3d11_helpers.hpp"
#include <d3d11.h>
#include <windows.h>
#include <GL/gl.h>
#include <plog/Log.h>
#elif NOZZLE_PLATFORM_LINUX
#include <nozzle/backends/linux.hpp>
#include <EGL/egl.h>
#include <GL/gl.h>
#endif

namespace nozzle::gl {

namespace {

#ifndef GL_RGBA8
#define GL_RGBA8 0x8058
#endif

#ifndef GL_RGBA16F
#define GL_RGBA16F 0x881A
#endif

#ifndef GL_HALF_FLOAT
#define GL_HALF_FLOAT 0x140B
#endif

#ifndef GL_BGRA
#define GL_BGRA 0x80E1
#endif

#ifndef GL_RG
#define GL_RG 0x8227
#endif

#ifndef GL_RED
#define GL_RED 0x1903
#endif

#ifndef GL_RGBA32F
#define GL_RGBA32F 0x8814
#endif

#ifndef GL_R8
#define GL_R8 0x8229
#endif

#ifndef GL_RG8
#define GL_RG8 0x822B
#endif

#ifndef GL_R16F
#define GL_R16F 0x822D
#endif

#ifndef GL_RG16F
#define GL_RG16F 0x822F
#endif

#ifndef GL_R32F
#define GL_R32F 0x822E
#endif

#ifndef GL_RG32F
#define GL_RG32F 0x8230
#endif

#ifndef GL_SRGB8_ALPHA8
#define GL_SRGB8_ALPHA8 0x8C43
#endif

#ifndef GL_R16
#define GL_R16 0x822A
#endif

#ifndef GL_RG16
#define GL_RG16 0x822C
#endif

#ifndef GL_RGBA16
#define GL_RGBA16 0x805B
#endif

#ifndef GL_R32UI
#define GL_R32UI 0x8236
#endif

#ifndef GL_RGBA32UI
#define GL_RGBA32UI 0x8C70
#endif

#ifndef GL_RGB
#define GL_RGB 0x1907
#endif

#ifndef GL_RGB8
#define GL_RGB8 0x8051
#endif

#ifndef GL_RGB16F
#define GL_RGB16F 0x8C3B
#endif

#ifndef GL_RGB32F
#define GL_RGB32F 0x8C3E
#endif

#ifndef GL_RGB32UI
#define GL_RGB32UI 0x8D71
#endif

#ifndef GL_RGB_INTEGER
#define GL_RGB_INTEGER 0x8D98
#endif

#ifndef GL_READ_FRAMEBUFFER
#define GL_READ_FRAMEBUFFER 0x8CA8
#endif

#ifndef GL_DRAW_FRAMEBUFFER
#define GL_DRAW_FRAMEBUFFER 0x8CA9
#endif

#ifndef GL_READ_FRAMEBUFFER_BINDING
#define GL_READ_FRAMEBUFFER_BINDING 0x8CAA
#endif

#ifndef GL_DRAW_FRAMEBUFFER_BINDING
#define GL_DRAW_FRAMEBUFFER_BINDING 0x8CA6
#endif

#ifndef GL_COLOR_ATTACHMENT0
#define GL_COLOR_ATTACHMENT0 0x8CE0
#endif

#ifndef GL_FRAMEBUFFER_COMPLETE
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#endif

#ifndef GL_RED_INTEGER
#define GL_RED_INTEGER 0x8D94
#endif

#ifndef GL_RGBA_INTEGER
#define GL_RGBA_INTEGER 0x8D99
#endif

#ifndef GL_UNSIGNED_SHORT
#define GL_UNSIGNED_SHORT 0x1403
#endif

#ifndef GL_UNSIGNED_INT
#define GL_UNSIGNED_INT 0x1405
#endif

struct gl_format_mapping {
    uint32_t internal_format;
    uint32_t format;
    uint32_t type;
};

bool map_format(texture_format fmt, gl_format_mapping &out) {
    switch (fmt) {
        case texture_format::r8_unorm:
            out = {GL_R8, GL_RED, GL_UNSIGNED_BYTE};
            return true;
        case texture_format::rg8_unorm:
            out = {GL_RG8, GL_RG, GL_UNSIGNED_BYTE};
            return true;
        case texture_format::rgb8_unorm:
            out = {GL_RGB8, GL_RGB, GL_UNSIGNED_BYTE};
            return true;
        case texture_format::rgba8_unorm:
            out = {GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE};
            return true;
        case texture_format::bgra8_unorm:
            out = {GL_RGBA8, GL_BGRA, GL_UNSIGNED_BYTE};
            return true;
        case texture_format::rgba8_srgb:
            out = {GL_SRGB8_ALPHA8, GL_RGBA, GL_UNSIGNED_BYTE};
            return true;
        case texture_format::bgra8_srgb:
            out = {GL_SRGB8_ALPHA8, GL_BGRA, GL_UNSIGNED_BYTE};
            return true;
        case texture_format::r16_unorm:
            out = {GL_R16, GL_RED, GL_UNSIGNED_SHORT};
            return true;
        case texture_format::rg16_unorm:
            out = {GL_RG16, GL_RG, GL_UNSIGNED_SHORT};
            return true;
        case texture_format::rgb16_unorm:
            out = {GL_RGB, GL_RGB, GL_UNSIGNED_SHORT};
            return true;
        case texture_format::rgba16_unorm:
            out = {GL_RGBA16, GL_RGBA, GL_UNSIGNED_SHORT};
            return true;
        case texture_format::r16_float:
            out = {GL_R16F, GL_RED, GL_HALF_FLOAT};
            return true;
        case texture_format::rg16_float:
            out = {GL_RG16F, GL_RG, GL_HALF_FLOAT};
            return true;
        case texture_format::rgb16_float:
            out = {GL_RGB16F, GL_RGB, GL_HALF_FLOAT};
            return true;
        case texture_format::rgba16_float:
            out = {GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT};
            return true;
        case texture_format::r32_float:
            out = {GL_R32F, GL_RED, GL_FLOAT};
            return true;
        case texture_format::rg32_float:
            out = {GL_RG32F, GL_RG, GL_FLOAT};
            return true;
        case texture_format::rgb32_float:
            out = {GL_RGB32F, GL_RGB, GL_FLOAT};
            return true;
        case texture_format::rgba32_float:
            out = {GL_RGBA32F, GL_RGBA, GL_FLOAT};
            return true;
        case texture_format::r32_uint:
            out = {GL_R32UI, GL_RED_INTEGER, GL_UNSIGNED_INT};
            return true;
        case texture_format::rgba32_uint:
            out = {GL_RGBA32UI, GL_RGBA_INTEGER, GL_UNSIGNED_INT};
            return true;
        case texture_format::rgb32_uint:
            out = {GL_RGB32UI, GL_RGB_INTEGER, GL_UNSIGNED_INT};
            return true;
        default:
            return false;
    }
}

uint32_t gl_type_size(uint32_t gl_type) {
    switch (gl_type) {
        case GL_UNSIGNED_BYTE:  return 1;
        case GL_UNSIGNED_SHORT: return 2;
        case GL_HALF_FLOAT:     return 2;
        case GL_FLOAT:          return 4;
        case GL_UNSIGNED_INT:   return 4;
        default:                return 0;
    }
}

uint32_t gl_format_components(uint32_t gl_format) {
    switch (gl_format) {
        case GL_RED:
        case GL_RED_INTEGER:    return 1;
        case GL_RG:             return 2;
        case GL_RGB:            return 3;
        case GL_RGBA:
        case GL_BGRA:
        case GL_RGBA_INTEGER:   return 4;
        default:                return 0;
    }
}

#if NOZZLE_PLATFORM_MACOS

struct scoped_fbo {
    GLuint read_fbo{0};
    GLuint draw_fbo{0};

    scoped_fbo() {
        glGenFramebuffers(1, &read_fbo);
        glGenFramebuffers(1, &draw_fbo);
    }

    ~scoped_fbo() {
        if (draw_fbo) { glDeleteFramebuffers(1, &draw_fbo); }
        if (read_fbo) { glDeleteFramebuffers(1, &read_fbo); }
    }
};

struct scoped_texture {
    GLuint name{0};

    scoped_texture() { glGenTextures(1, &name); }
    ~scoped_texture() { if (name) { glDeleteTextures(1, &name); } }
};

#endif // NOZZLE_PLATFORM_MACOS

#if NOZZLE_PLATFORM_WINDOWS

#ifndef WGL_ACCESS_READ_ONLY_NV
#define WGL_ACCESS_READ_ONLY_NV 0x0000
#endif

#ifndef WGL_ACCESS_READ_WRITE_NV
#define WGL_ACCESS_READ_WRITE_NV 0x0001
#endif

#ifndef WGL_ACCESS_WRITE_DISCARD_NV
#define WGL_ACCESS_WRITE_DISCARD_NV 0x0002
#endif

using gl_gen_framebuffers_proc = void (APIENTRY *)(GLsizei, GLuint *);
using gl_delete_framebuffers_proc = void (APIENTRY *)(GLsizei, const GLuint *);
using gl_bind_framebuffer_proc = void (APIENTRY *)(GLenum, GLuint);
using gl_framebuffer_texture_2d_proc = void (APIENTRY *)(GLenum, GLenum, GLenum, GLuint, GLint);
using gl_check_framebuffer_status_proc = GLenum (APIENTRY *)(GLenum);
using gl_blit_framebuffer_proc = void (APIENTRY *)(
    GLint, GLint, GLint, GLint,
    GLint, GLint, GLint, GLint,
    GLbitfield, GLenum);

using wgl_get_extensions_string_arb_proc = const char *(WINAPI *)(HDC);
using wgl_get_extensions_string_ext_proc = const char *(WINAPI *)(void);
using wgl_dx_open_device_nv_proc = HANDLE (WINAPI *)(void *);
using wgl_dx_close_device_nv_proc = BOOL (WINAPI *)(HANDLE);
using wgl_dx_register_object_nv_proc = HANDLE (WINAPI *)(HANDLE, void *, GLuint, GLenum, GLenum);
using wgl_dx_unregister_object_nv_proc = BOOL (WINAPI *)(HANDLE, HANDLE);
using wgl_dx_object_access_nv_proc = BOOL (WINAPI *)(HANDLE, GLenum);
using wgl_dx_lock_objects_nv_proc = BOOL (WINAPI *)(HANDLE, GLint, HANDLE *);
using wgl_dx_unlock_objects_nv_proc = BOOL (WINAPI *)(HANDLE, GLint, HANDLE *);

struct windows_gl_fbo_functions {
    gl_gen_framebuffers_proc gen_framebuffers{nullptr};
    gl_delete_framebuffers_proc delete_framebuffers{nullptr};
    gl_bind_framebuffer_proc bind_framebuffer{nullptr};
    gl_framebuffer_texture_2d_proc framebuffer_texture_2d{nullptr};
    gl_check_framebuffer_status_proc check_framebuffer_status{nullptr};
    gl_blit_framebuffer_proc blit_framebuffer{nullptr};
};

struct windows_wgl_dx_interop_functions {
    wgl_dx_open_device_nv_proc open_device{nullptr};
    wgl_dx_close_device_nv_proc close_device{nullptr};
    wgl_dx_register_object_nv_proc register_object{nullptr};
    wgl_dx_unregister_object_nv_proc unregister_object{nullptr};
    wgl_dx_object_access_nv_proc object_access{nullptr};
    wgl_dx_lock_objects_nv_proc lock_objects{nullptr};
    wgl_dx_unlock_objects_nv_proc unlock_objects{nullptr};
};

void *load_gl_proc(const char *name) {
    PROC proc = wglGetProcAddress(name);
    uintptr_t value = reinterpret_cast<uintptr_t>(proc);
    if (value <= 3 || value == static_cast<uintptr_t>(-1)) {
        return nullptr;
    }
    return reinterpret_cast<void *>(proc);
}

bool load_windows_gl_fbo_functions(windows_gl_fbo_functions &functions) {
    functions.gen_framebuffers = reinterpret_cast<gl_gen_framebuffers_proc>(
        load_gl_proc("glGenFramebuffers"));
    functions.delete_framebuffers = reinterpret_cast<gl_delete_framebuffers_proc>(
        load_gl_proc("glDeleteFramebuffers"));
    functions.bind_framebuffer = reinterpret_cast<gl_bind_framebuffer_proc>(
        load_gl_proc("glBindFramebuffer"));
    functions.framebuffer_texture_2d = reinterpret_cast<gl_framebuffer_texture_2d_proc>(
        load_gl_proc("glFramebufferTexture2D"));
    functions.check_framebuffer_status = reinterpret_cast<gl_check_framebuffer_status_proc>(
        load_gl_proc("glCheckFramebufferStatus"));
    functions.blit_framebuffer = reinterpret_cast<gl_blit_framebuffer_proc>(
        load_gl_proc("glBlitFramebuffer"));

    return functions.gen_framebuffers &&
        functions.delete_framebuffers &&
        functions.bind_framebuffer &&
        functions.framebuffer_texture_2d &&
        functions.check_framebuffer_status &&
        functions.blit_framebuffer;
}

bool load_windows_wgl_dx_interop_functions(windows_wgl_dx_interop_functions &functions) {
    functions.open_device = reinterpret_cast<wgl_dx_open_device_nv_proc>(
        load_gl_proc("wglDXOpenDeviceNV"));
    functions.close_device = reinterpret_cast<wgl_dx_close_device_nv_proc>(
        load_gl_proc("wglDXCloseDeviceNV"));
    functions.register_object = reinterpret_cast<wgl_dx_register_object_nv_proc>(
        load_gl_proc("wglDXRegisterObjectNV"));
    functions.unregister_object = reinterpret_cast<wgl_dx_unregister_object_nv_proc>(
        load_gl_proc("wglDXUnregisterObjectNV"));
    functions.object_access = reinterpret_cast<wgl_dx_object_access_nv_proc>(
        load_gl_proc("wglDXObjectAccessNV"));
    functions.lock_objects = reinterpret_cast<wgl_dx_lock_objects_nv_proc>(
        load_gl_proc("wglDXLockObjectsNV"));
    functions.unlock_objects = reinterpret_cast<wgl_dx_unlock_objects_nv_proc>(
        load_gl_proc("wglDXUnlockObjectsNV"));

    return functions.open_device &&
        functions.close_device &&
        functions.register_object &&
        functions.unregister_object &&
        functions.object_access &&
        functions.lock_objects &&
        functions.unlock_objects;
}

bool extension_list_contains(const char *extensions, const char *needle) {
    if (!extensions || !needle || needle[0] == '\0') {
        return false;
    }

    std::string haystack{extensions};
    std::string token{needle};
    size_t offset = 0;
    while (offset < haystack.size()) {
        size_t next_space = haystack.find(' ', offset);
        size_t length = next_space == std::string::npos
            ? haystack.size() - offset
            : next_space - offset;
        if (length == token.size() &&
            haystack.compare(offset, length, token) == 0) {
            return true;
        }
        if (next_space == std::string::npos) {
            break;
        }
        offset = next_space + 1;
    }
    return false;
}

bool current_wgl_context_has_extension(const char *extension_name) {
    HDC device_context = wglGetCurrentDC();
    auto get_extensions_arb = reinterpret_cast<wgl_get_extensions_string_arb_proc>(
        load_gl_proc("wglGetExtensionsStringARB"));
    if (get_extensions_arb && device_context) {
        if (extension_list_contains(get_extensions_arb(device_context), extension_name)) {
            return true;
        }
    }

    auto get_extensions_ext = reinterpret_cast<wgl_get_extensions_string_ext_proc>(
        load_gl_proc("wglGetExtensionsStringEXT"));
    return get_extensions_ext &&
        extension_list_contains(get_extensions_ext(), extension_name);
}

struct scoped_windows_gl_texture {
    GLuint name{0};

    scoped_windows_gl_texture() {
        glGenTextures(1, &name);
    }

    ~scoped_windows_gl_texture() {
        if (name != 0) {
            glDeleteTextures(1, &name);
        }
    }
};

struct scoped_windows_fbos {
    const windows_gl_fbo_functions *functions{nullptr};
    GLuint read_fbo{0};
    GLuint draw_fbo{0};

    explicit scoped_windows_fbos(const windows_gl_fbo_functions &fbo_functions)
        : functions{&fbo_functions}
    {
        functions->gen_framebuffers(1, &read_fbo);
        functions->gen_framebuffers(1, &draw_fbo);
    }

    ~scoped_windows_fbos() {
        if (!functions) {
            return;
        }
        if (draw_fbo != 0) {
            functions->delete_framebuffers(1, &draw_fbo);
        }
        if (read_fbo != 0) {
            functions->delete_framebuffers(1, &read_fbo);
        }
    }
};

struct scoped_d3d11_publish_mutex {
    IDXGIKeyedMutex *mutex{nullptr};
    bool acquired{false};

    ~scoped_d3d11_publish_mutex() {
        if (mutex) {
            if (acquired) {
                (void)mutex->ReleaseSync(0);
            }
            mutex->Release();
        }
    }

    Result<void> acquire(ID3D11Texture2D *texture) {
        HRESULT hr = texture->QueryInterface(
            __uuidof(IDXGIKeyedMutex), reinterpret_cast<void **>(&mutex));
        if (FAILED(hr) || !mutex) {
            return Error{ErrorCode::UnsupportedBackend,
                "D3D11 texture has no keyed mutex for WGL_NV_DX_interop2 publish"};
        }

        hr = d3d11::acquire_publish_mutex(mutex);
        if (hr != S_OK) {
            return Error{ErrorCode::Timeout,
                "timeout acquiring D3D11 publish keyed mutex for WGL_NV_DX_interop2"};
        }
        acquired = true;
        return {};
    }

    Result<void> release_unpublished() {
        if (!mutex || !acquired) {
            return Error{ErrorCode::BackendError,
                "D3D11 publish keyed mutex was not acquired"};
        }
        HRESULT hr = mutex->ReleaseSync(0);
        acquired = false;
        if (FAILED(hr)) {
            return Error{ErrorCode::BackendError,
                "failed to release D3D11 publish keyed mutex after OpenGL write"};
        }
        return {};
    }
};

void log_windows_gl_cpu_fallback_once(const Error &error) {
    static std::mutex mutex;
    static bool logged = false;
    std::lock_guard<std::mutex> lock{mutex};
    if (logged) {
        return;
    }
    logged = true;
    LOG_WARNING << "Windows GL publish falling back to CPU readback/staging path: "
        << error.message;
}

Result<void> copy_gl_texture_to_d3d11_staging(
    const gl_texture_desc &gl_desc,
    const gl_format_mapping &gl_fmt,
    ID3D11Texture2D *d3d_tex) {
    scoped_d3d11_publish_mutex publish_mutex;
    auto mutex_result = publish_mutex.acquire(d3d_tex);
    if (!mutex_result.ok()) {
        return mutex_result.error();
    }

    uint32_t bpp = gl_format_components(gl_fmt.format) * gl_type_size(gl_fmt.type);
    uint32_t row_bytes = gl_desc.width * bpp;
    std::vector<uint8_t> pixel_data(static_cast<size_t>(row_bytes) * gl_desc.height);

    GLint prev_pack = 0;
    glGetIntegerv(GL_PACK_ALIGNMENT, &prev_pack);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glBindTexture(GL_TEXTURE_2D, gl_desc.name);
    glGetTexImage(GL_TEXTURE_2D, 0, gl_fmt.format, gl_fmt.type, pixel_data.data());
    glPixelStorei(GL_PACK_ALIGNMENT, prev_pack);
    glBindTexture(GL_TEXTURE_2D, 0);

    if (glGetError() != GL_NO_ERROR) {
        return Error{ErrorCode::BackendError, "glGetTexImage failed in Windows GL CPU fallback"};
    }

    ID3D11Device *d3d_device = nullptr;
    d3d_tex->GetDevice(&d3d_device);
    if (!d3d_device) {
        return Error{ErrorCode::BackendError, "failed to get D3D11 device from texture"};
    }

    ID3D11DeviceContext *d3d_ctx = nullptr;
    d3d_device->GetImmediateContext(&d3d_ctx);
    if (!d3d_ctx) {
        d3d_device->Release();
        return Error{ErrorCode::BackendError, "failed to get D3D11 immediate context"};
    }

    D3D11_TEXTURE2D_DESC tex_desc{};
    d3d_tex->GetDesc(&tex_desc);
    tex_desc.Usage = D3D11_USAGE_STAGING;
    tex_desc.BindFlags = 0;
    tex_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    tex_desc.MiscFlags = 0;

    ID3D11Texture2D *staging = nullptr;
    HRESULT hr = d3d_device->CreateTexture2D(&tex_desc, nullptr, &staging);
    if (FAILED(hr) || !staging) {
        d3d_ctx->Release();
        d3d_device->Release();
        return Error{ErrorCode::ResourceCreationFailed, "failed to create staging texture"};
    }

    D3D11_MAPPED_SUBRESOURCE mapped{};
    hr = d3d_ctx->Map(staging, 0, D3D11_MAP_WRITE, 0, &mapped);
    if (FAILED(hr)) {
        staging->Release();
        d3d_ctx->Release();
        d3d_device->Release();
        return Error{ErrorCode::BackendError, "failed to map staging texture"};
    }

    for (uint32_t y = 0; y < gl_desc.height; y = y + 1) {
        std::memcpy(
            static_cast<uint8_t *>(mapped.pData) + y * mapped.RowPitch,
            pixel_data.data() + y * row_bytes,
            row_bytes
        );
    }

    d3d_ctx->Unmap(staging, 0);
    d3d_ctx->CopySubresourceRegion(d3d_tex, 0, 0, 0, 0, staging, 0, nullptr);
    d3d_ctx->Flush();

    staging->Release();
    d3d_ctx->Release();
    d3d_device->Release();

    return publish_mutex.release_unpublished();
}

Result<void> publish_gl_texture_wgl_dx_interop2(
    const gl_texture_desc &gl_desc,
    ID3D11Texture2D *d3d_tex) {
    if (!current_wgl_context_has_extension("WGL_NV_DX_interop2")) {
        return Error{ErrorCode::UnsupportedBackend,
            "WGL_NV_DX_interop2 extension is not advertised by current WGL context"};
    }

    windows_gl_fbo_functions fbo_functions{};
    if (!load_windows_gl_fbo_functions(fbo_functions)) {
        return Error{ErrorCode::UnsupportedBackend,
            "required OpenGL FBO/blit entry points are unavailable"};
    }

    windows_wgl_dx_interop_functions interop_functions{};
    if (!load_windows_wgl_dx_interop_functions(interop_functions)) {
        return Error{ErrorCode::UnsupportedBackend,
            "WGL_NV_DX_interop2 entry points are unavailable"};
    }

    ID3D11Device *d3d_device = nullptr;
    d3d_tex->GetDevice(&d3d_device);
    if (!d3d_device) {
        return Error{ErrorCode::BackendError, "failed to get D3D11 device from texture"};
    }

    D3D11_TEXTURE2D_DESC tex_desc{};
    d3d_tex->GetDesc(&tex_desc);
    if (tex_desc.Usage != D3D11_USAGE_DEFAULT ||
        tex_desc.SampleDesc.Count != 1 ||
        tex_desc.ArraySize != 1 ||
        tex_desc.MipLevels != 1 ||
        tex_desc.Width != gl_desc.width ||
        tex_desc.Height != gl_desc.height) {
        d3d_device->Release();
        return Error{ErrorCode::UnsupportedBackend,
            "D3D11 texture is not compatible with WGL_NV_DX_interop2 publish"};
    }

    scoped_d3d11_publish_mutex publish_mutex;
    auto mutex_result = publish_mutex.acquire(d3d_tex);
    if (!mutex_result.ok()) {
        d3d_device->Release();
        return mutex_result.error();
    }

    HANDLE interop_device = interop_functions.open_device(d3d_device);
    d3d_device->Release();
    if (!interop_device) {
        return Error{ErrorCode::UnsupportedBackend,
            "wglDXOpenDeviceNV failed"};
    }

    scoped_windows_gl_texture interop_texture;
    if (interop_texture.name == 0) {
        interop_functions.close_device(interop_device);
        return Error{ErrorCode::ResourceCreationFailed,
            "failed to create GL texture name for WGL_NV_DX_interop2"};
    }

    HANDLE interop_object = interop_functions.register_object(
        interop_device,
        d3d_tex,
        interop_texture.name,
        GL_TEXTURE_2D,
        WGL_ACCESS_WRITE_DISCARD_NV);
    if (!interop_object) {
        interop_functions.close_device(interop_device);
        return Error{ErrorCode::UnsupportedBackend,
            "wglDXRegisterObjectNV failed"};
    }

    bool locked = false;
    auto cleanup_interop = [&]() -> Result<void> {
        bool failed = false;
        if (locked) {
            HANDLE object = interop_object;
            if (!interop_functions.unlock_objects(interop_device, 1, &object)) {
                failed = true;
            }
            locked = false;
        }
        if (interop_object) {
            if (!interop_functions.unregister_object(interop_device, interop_object)) {
                LOG_WARNING << "wglDXUnregisterObjectNV failed during WGL_NV_DX_interop2 cleanup";
            }
            interop_object = nullptr;
        }
        if (interop_device) {
            if (!interop_functions.close_device(interop_device)) {
                LOG_WARNING << "wglDXCloseDeviceNV failed during WGL_NV_DX_interop2 cleanup";
            }
            interop_device = nullptr;
        }
        if (failed) {
            return Error{ErrorCode::BackendError,
                "wglDXUnlockObjectsNV failed during WGL_NV_DX_interop2 publish"};
        }
        return {};
    };

    if (!interop_functions.object_access(interop_object, WGL_ACCESS_WRITE_DISCARD_NV)) {
        auto cleanup_result = cleanup_interop();
        if (!cleanup_result.ok()) {
            return cleanup_result.error();
        }
        return Error{ErrorCode::UnsupportedBackend,
            "wglDXObjectAccessNV failed"};
    }

    HANDLE lock_object = interop_object;
    if (!interop_functions.lock_objects(interop_device, 1, &lock_object)) {
        auto cleanup_result = cleanup_interop();
        if (!cleanup_result.ok()) {
            return cleanup_result.error();
        }
        return Error{ErrorCode::UnsupportedBackend,
            "wglDXLockObjectsNV failed"};
    }
    locked = true;

    GLint previous_read_fbo = 0;
    GLint previous_draw_fbo = 0;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &previous_read_fbo);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &previous_draw_fbo);

    scoped_windows_fbos fbos{fbo_functions};
    fbo_functions.bind_framebuffer(GL_READ_FRAMEBUFFER, fbos.read_fbo);
    fbo_functions.framebuffer_texture_2d(
        GL_READ_FRAMEBUFFER,
        GL_COLOR_ATTACHMENT0,
        gl_desc.target,
        gl_desc.name,
        0);
    if (fbo_functions.check_framebuffer_status(GL_READ_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        fbo_functions.bind_framebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(previous_read_fbo));
        fbo_functions.bind_framebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(previous_draw_fbo));
        auto cleanup_result = cleanup_interop();
        if (!cleanup_result.ok()) {
            return cleanup_result.error();
        }
        return Error{ErrorCode::BackendError,
            "source GL framebuffer is incomplete for WGL_NV_DX_interop2 publish"};
    }

    fbo_functions.bind_framebuffer(GL_DRAW_FRAMEBUFFER, fbos.draw_fbo);
    fbo_functions.framebuffer_texture_2d(
        GL_DRAW_FRAMEBUFFER,
        GL_COLOR_ATTACHMENT0,
        GL_TEXTURE_2D,
        interop_texture.name,
        0);
    if (fbo_functions.check_framebuffer_status(GL_DRAW_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        fbo_functions.bind_framebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(previous_read_fbo));
        fbo_functions.bind_framebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(previous_draw_fbo));
        auto cleanup_result = cleanup_interop();
        if (!cleanup_result.ok()) {
            return cleanup_result.error();
        }
        return Error{ErrorCode::BackendError,
            "destination GL framebuffer is incomplete for WGL_NV_DX_interop2 publish"};
    }

    fbo_functions.blit_framebuffer(
        0, 0,
        static_cast<GLint>(gl_desc.width),
        static_cast<GLint>(gl_desc.height),
        0, 0,
        static_cast<GLint>(gl_desc.width),
        static_cast<GLint>(gl_desc.height),
        GL_COLOR_BUFFER_BIT,
        GL_NEAREST);

    GLenum gl_err = glGetError();
    fbo_functions.bind_framebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(previous_read_fbo));
    fbo_functions.bind_framebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(previous_draw_fbo));
    if (gl_err != GL_NO_ERROR) {
        auto cleanup_result = cleanup_interop();
        if (!cleanup_result.ok()) {
            return cleanup_result.error();
        }
        return Error{ErrorCode::BackendError,
            "GL blit failed during WGL_NV_DX_interop2 publish"};
    }

    glFlush();
    gl_err = glGetError();
    auto cleanup_result = cleanup_interop();
    if (!cleanup_result.ok()) {
        return cleanup_result.error();
    }
    if (gl_err != GL_NO_ERROR) {
        return Error{ErrorCode::BackendError,
            "GL flush failed during WGL_NV_DX_interop2 publish"};
    }

    return publish_mutex.release_unpublished();
}

#endif // NOZZLE_PLATFORM_WINDOWS

} // anonymous namespace

#if NOZZLE_PLATFORM_MACOS

namespace {

// macOS CGL + IOSurface measured blit rects.
// CGLTexImageIOSurface2D maps GL texel row 0 (y=0, bottom in GL coords)
// to IOSurface memory row 0 (= image top, canonical top_left).
// This means the CGL binding already performs the bottom_left→top_left
// conversion implicitly. Therefore straight blit is always correct for
// publish, and the destination-side flip is needed only when the caller
// explicitly requests top_left output from a canonical top_left source.
//
// These rects are based on empirical measurement (Issue #7), not OpenGL theory.

void blit_publish_to_canonical_macos_cgl_iosurface(
    texture_origin src_origin, GLint w, GLint h) {
    // CGL IOSurface binding already aligns GL row 0 with IOSurface row 0.
    // Straight blit is correct regardless of declared source origin.
    // TODO: verify if top_left source needs different rect
    (void)src_origin;
    glBlitFramebuffer(0, 0, w, h, 0, 0, w, h,
                      GL_COLOR_BUFFER_BIT, GL_NEAREST);
}

void blit_canonical_to_dest_macos_cgl_iosurface(
    texture_origin dst_origin, GLint w, GLint h) {
    if (dst_origin == texture_origin::bottom_left) {
        // CGL IOSurface binding maps GL row 0 to IOSurface row 0 (top).
        // GL expects row 0 at bottom, so straight blit gives correct GL orientation.
        glBlitFramebuffer(0, 0, w, h, 0, 0, w, h,
                          GL_COLOR_BUFFER_BIT, GL_NEAREST);
    } else {
        // top_left destination: IOSurface row 0 (top) → GL row 0 (bottom),
        // but caller wants top_left, so flip to put top at top.
        glBlitFramebuffer(0, h, w, 0, 0, 0, w, h,
                          GL_COLOR_BUFFER_BIT, GL_NEAREST);
    }
}

} // anonymous namespace

Result<void> publish_gl_texture(sender &snd, const gl_texture_desc &gl_desc) {
    if (gl_desc.name == 0) {
        return Error{ErrorCode::InvalidArgument, "GL texture name is 0"};
    }
    if (!CGLGetCurrentContext()) {
        return Error{ErrorCode::InvalidArgument, "no current GL context"};
    }

    gl_format_mapping gl_fmt;
    if (!map_format(gl_desc.format, gl_fmt)) {
        return Error{ErrorCode::UnsupportedFormat, "unsupported format for GL interop"};
    }

    if (gl_desc.format == texture_format::rgba8_srgb ||
        gl_desc.format == texture_format::bgra8_srgb) {
        return Error{ErrorCode::UnsupportedFormat,
            "macOS GL publish does not support sRGB IOSurface storage"};
    }

    // CGLTexImageIOSurface2D is reliable for 8-bit color IOSurfaces through
    // BGRA binding. Keep the caller-requested format as semantic metadata, but
    // materialize RGBA8 GL sources into BGRA8 IOSurface storage.
    texture_format storage_format = gl_desc.format;
    if (gl_desc.format == texture_format::rgba8_unorm) {
        storage_format = texture_format::bgra8_unorm;
    }

    nozzle::texture_desc td{};
    td.width = gl_desc.width;
    td.height = gl_desc.height;
    td.format = storage_format;
    td.semantic_format = gl_desc.format;
    td.swizzle = channel_swizzle::identity;

    auto frame_result = snd.acquire_writable_frame(td);
    if (!frame_result) { return frame_result.error(); }

    auto &wframe = frame_result.value();
    texture_format publish_format = wframe.get_texture().desc().format;
    if (publish_format != storage_format) {
        return Error{ErrorCode::UnsupportedFormat,
            "GL publish acquired an unexpected storage format"};
    }

    void *surface_ptr = metal::get_io_surface(wframe.get_texture());
    if (!surface_ptr) {
        return Error{ErrorCode::BackendError, "no IOSurface in writable frame"};
    }

    IOSurfaceRef surface = static_cast<IOSurfaceRef>(surface_ptr);

    scoped_texture io_tex;
    glBindTexture(GL_TEXTURE_RECTANGLE_ARB, io_tex.name);

    CGLError cgl_err;
    if (publish_format == texture_format::bgra8_unorm) {
        cgl_err = CGLTexImageIOSurface2D(
            CGLGetCurrentContext(), GL_TEXTURE_RECTANGLE_ARB, GL_RGBA8,
            static_cast<GLsizei>(gl_desc.width),
            static_cast<GLsizei>(gl_desc.height),
            GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, surface, 0);
    } else {
        cgl_err = CGLTexImageIOSurface2D(
            CGLGetCurrentContext(), GL_TEXTURE_RECTANGLE_ARB, gl_fmt.internal_format,
            static_cast<GLsizei>(gl_desc.width),
            static_cast<GLsizei>(gl_desc.height),
            gl_fmt.format, gl_fmt.type, surface, 0);
    }
    if (cgl_err != kCGLNoError) {
        return Error{ErrorCode::BackendError, "CGLTexImageIOSurface2D failed"};
    }

    auto src_w = static_cast<GLint>(gl_desc.width);
    auto src_h = static_cast<GLint>(gl_desc.height);

    scoped_fbo fbos;

    glBindFramebuffer(GL_READ_FRAMEBUFFER, fbos.read_fbo);
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           gl_desc.target, gl_desc.name, 0);

    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbos.draw_fbo);
    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_RECTANGLE_ARB, io_tex.name, 0);

    blit_publish_to_canonical_macos_cgl_iosurface(gl_desc.origin, src_w, src_h);

    GLenum gl_err = glGetError();
    if (gl_err != GL_NO_ERROR) {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        return Error{ErrorCode::BackendError, "GL blit failed in publish"};
    }

    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glFlush();

    GLenum flush_err = glGetError();
    if (flush_err != GL_NO_ERROR) {
        return Error{ErrorCode::BackendError, "GL flush failed in publish"};
    }

    auto commit_result = snd.commit_frame(wframe);
    if (!commit_result) { return commit_result.error(); }

    return {};
}

Result<void> copy_frame_to_gl_texture(const frame &frm, const gl_texture_desc &gl_desc) {
    if (gl_desc.name == 0) {
        return Error{ErrorCode::InvalidArgument, "GL texture name is 0"};
    }
    if (!frm.valid()) {
        return Error{ErrorCode::InvalidArgument, "frame is not valid"};
    }
    if (!CGLGetCurrentContext()) {
        return Error{ErrorCode::InvalidArgument, "no current GL context"};
    }

    gl_format_mapping gl_fmt;
    if (!map_format(gl_desc.format, gl_fmt)) {
        return Error{ErrorCode::UnsupportedFormat, "unsupported format for GL interop"};
    }

    void *surface_ptr = metal::get_io_surface(frm.get_texture());
    if (!surface_ptr) {
        return Error{ErrorCode::BackendError, "no IOSurface in frame"};
    }

    IOSurfaceRef surface = static_cast<IOSurfaceRef>(surface_ptr);

    scoped_texture io_tex;
    glBindTexture(GL_TEXTURE_RECTANGLE_ARB, io_tex.name);

    CGLError cgl_err = CGLTexImageIOSurface2D(
        CGLGetCurrentContext(),
        GL_TEXTURE_RECTANGLE_ARB,
        gl_fmt.internal_format,
        static_cast<GLsizei>(gl_desc.width),
        static_cast<GLsizei>(gl_desc.height),
        gl_fmt.format,
        gl_fmt.type,
        surface,
        0
    );
    if (cgl_err != kCGLNoError) {
        return Error{ErrorCode::BackendError, "CGLTexImageIOSurface2D failed"};
    }

    scoped_fbo fbos;

    glBindFramebuffer(GL_READ_FRAMEBUFFER, fbos.read_fbo);
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_RECTANGLE_ARB, io_tex.name, 0);

    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbos.draw_fbo);
    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           gl_desc.target, gl_desc.name, 0);

    blit_canonical_to_dest_macos_cgl_iosurface(gl_desc.origin,
        static_cast<GLint>(gl_desc.width), static_cast<GLint>(gl_desc.height));

    GLenum gl_err = glGetError();
    if (gl_err != GL_NO_ERROR) {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        return Error{ErrorCode::BackendError, "GL blit failed in copy_to_gl"};
    }

    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);

    return {};
}

#elif NOZZLE_PLATFORM_WINDOWS

Result<void> publish_gl_texture(sender &snd, const gl_texture_desc &gl_desc) {
    if (gl_desc.name == 0) {
        return Error{ErrorCode::InvalidArgument, "GL texture name is 0"};
    }
    if (!wglGetCurrentContext()) {
        return Error{ErrorCode::InvalidArgument, "no current GL context"};
    }

    gl_format_mapping gl_fmt;
    if (!map_format(gl_desc.format, gl_fmt)) {
        return Error{ErrorCode::UnsupportedFormat, "unsupported format for GL interop"};
    }

    nozzle::texture_desc td{};
    td.width = gl_desc.width;
    td.height = gl_desc.height;
    td.format = gl_desc.format;

    auto frame_result = snd.acquire_writable_frame(td);
    if (!frame_result) { return frame_result.error(); }

    auto &wframe = frame_result.value();

    ID3D11Texture2D *d3d_tex = d3d11::get_texture(wframe.get_texture());
    if (!d3d_tex) {
        return Error{ErrorCode::BackendError, "no D3D11 texture in writable frame"};
    }

    auto interop_result = publish_gl_texture_wgl_dx_interop2(gl_desc, d3d_tex);
    if (!interop_result.ok()) {
        if (interop_result.error().code != ErrorCode::UnsupportedBackend) {
            (void)snd.discard_frame(wframe);
            return interop_result.error();
        }
        log_windows_gl_cpu_fallback_once(interop_result.error());
        auto fallback_result = copy_gl_texture_to_d3d11_staging(gl_desc, gl_fmt, d3d_tex);
        if (!fallback_result.ok()) {
            (void)snd.discard_frame(wframe);
            return fallback_result.error();
        }
    }

    auto commit_result = snd.commit_frame(wframe);
    if (!commit_result) { return commit_result.error(); }

    return {};
}

Result<void> copy_frame_to_gl_texture(const frame &frm, const gl_texture_desc &gl_desc) {
    if (gl_desc.name == 0) {
        return Error{ErrorCode::InvalidArgument, "GL texture name is 0"};
    }
    if (!frm.valid()) {
        return Error{ErrorCode::InvalidArgument, "frame is not valid"};
    }
    if (!wglGetCurrentContext()) {
        return Error{ErrorCode::InvalidArgument, "no current GL context"};
    }

    gl_format_mapping gl_fmt;
    if (!map_format(gl_desc.format, gl_fmt)) {
        return Error{ErrorCode::UnsupportedFormat, "unsupported format for GL interop"};
    }

    ID3D11Texture2D *d3d_tex = d3d11::get_texture(frm.get_texture());
    if (!d3d_tex) {
        return Error{ErrorCode::BackendError, "no D3D11 texture in frame"};
    }

    ID3D11Device *d3d_device = nullptr;
    d3d_tex->GetDevice(&d3d_device);
    if (!d3d_device) {
        return Error{ErrorCode::BackendError, "failed to get D3D11 device from texture"};
    }

    ID3D11DeviceContext *d3d_ctx = nullptr;
    d3d_device->GetImmediateContext(&d3d_ctx);

    D3D11_TEXTURE2D_DESC tex_desc{};
    d3d_tex->GetDesc(&tex_desc);
    tex_desc.Usage = D3D11_USAGE_STAGING;
    tex_desc.BindFlags = 0;
    tex_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    tex_desc.MiscFlags = 0;

    ID3D11Texture2D *staging = nullptr;
    HRESULT hr = d3d_device->CreateTexture2D(&tex_desc, nullptr, &staging);
    if (FAILED(hr) || !staging) {
        d3d_ctx->Release();
        d3d_device->Release();
        return Error{ErrorCode::ResourceCreationFailed, "failed to create staging texture"};
    }

    d3d_ctx->CopySubresourceRegion(staging, 0, 0, 0, 0, d3d_tex, 0, nullptr);

    D3D11_MAPPED_SUBRESOURCE mapped{};
    hr = d3d_ctx->Map(staging, 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr)) {
        staging->Release();
        d3d_ctx->Release();
        d3d_device->Release();
        return Error{ErrorCode::BackendError, "failed to map staging texture"};
    }

    uint32_t bpp = gl_format_components(gl_fmt.format) * gl_type_size(gl_fmt.type);
    uint32_t row_bytes = gl_desc.width * bpp;
    std::vector<uint8_t> pixel_data(static_cast<size_t>(row_bytes) * gl_desc.height);

    for (uint32_t y = 0; y < gl_desc.height; ++y) {
        std::memcpy(
            pixel_data.data() + y * row_bytes,
            static_cast<uint8_t *>(mapped.pData) + y * mapped.RowPitch,
            row_bytes
        );
    }

    d3d_ctx->Unmap(staging, 0);
    staging->Release();
    d3d_ctx->Release();
    d3d_device->Release();

    GLint prev_unpack = 0;
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &prev_unpack);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glBindTexture(GL_TEXTURE_2D, gl_desc.name);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
        gl_desc.width, gl_desc.height,
        gl_fmt.format, gl_fmt.type, pixel_data.data());
    glPixelStorei(GL_UNPACK_ALIGNMENT, prev_unpack);
    glBindTexture(GL_TEXTURE_2D, 0);

    if (glGetError() != GL_NO_ERROR) {
        return Error{ErrorCode::BackendError, "glTexSubImage2D failed"};
    }

    return {};
}

#endif // NOZZLE_PLATFORM_WINDOWS

#if NOZZLE_PLATFORM_LINUX

Result<void> publish_gl_texture(sender &snd, const gl_texture_desc &gl_desc) {
    if (gl_desc.name == 0) {
        return Error{ErrorCode::InvalidArgument, "GL texture name is 0"};
    }

    gl_format_mapping gl_fmt;
    if (!map_format(gl_desc.format, gl_fmt)) {
        return Error{ErrorCode::UnsupportedFormat, "unsupported format for GL interop"};
    }

    uint32_t bpp = gl_format_components(gl_fmt.format) * gl_type_size(gl_fmt.type);
    uint32_t row_bytes = gl_desc.width * bpp;
    std::vector<uint8_t> pixel_data(static_cast<size_t>(row_bytes) * gl_desc.height);

    GLint prev_pack = 0;
    glGetIntegerv(GL_PACK_ALIGNMENT, &prev_pack);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glBindTexture(GL_TEXTURE_2D, gl_desc.name);
    glGetTexImage(GL_TEXTURE_2D, 0, gl_fmt.format, gl_fmt.type, pixel_data.data());
    glPixelStorei(GL_PACK_ALIGNMENT, prev_pack);
    glBindTexture(GL_TEXTURE_2D, 0);

    nozzle::texture_desc td{};
    td.width = gl_desc.width;
    td.height = gl_desc.height;
    td.format = gl_desc.format;

    auto frame_result = snd.acquire_writable_frame(td);
    if (!frame_result) { return frame_result.error(); }

    auto &wframe = frame_result.value();

    void *egl_image = dma_buf::get_egl_image(wframe.get_texture());
    if (!egl_image) {
        return Error{ErrorCode::BackendError, "no EGLImage in writable frame"};
    }

    // DMA-BUF CPU pixel write not yet implemented — cannot transfer GL pixels.
    return Error{ErrorCode::UnsupportedBackend,
        "Linux GL interop requires Phase 2 DMA-BUF pixel write (not yet implemented)"};
}

Result<void> copy_frame_to_gl_texture(const frame &frm, const gl_texture_desc &gl_desc) {
    if (gl_desc.name == 0) {
        return Error{ErrorCode::InvalidArgument, "GL texture name is 0"};
    }
    if (!frm.valid()) {
        return Error{ErrorCode::InvalidArgument, "frame is not valid"};
    }

    gl_format_mapping gl_fmt;
    if (!map_format(gl_desc.format, gl_fmt)) {
        return Error{ErrorCode::UnsupportedFormat, "unsupported format for GL interop"};
    }

    void *egl_image = dma_buf::get_egl_image(frm.get_texture());
    if (!egl_image) {
        return Error{ErrorCode::BackendError, "no EGLImage in frame"};
    }

    // DMA-BUF CPU pixel read not yet implemented.
    return Error{ErrorCode::UnsupportedBackend,
        "Linux GL interop requires Phase 2 EGLImage mapping (not yet implemented)"};
}

#endif // NOZZLE_PLATFORM_LINUX

} // namespace nozzle::gl
