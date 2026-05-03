// nozzle - opengl_backend.cpp - OpenGL interop (macOS: IOSurface blit, Windows: CPU copy)

#include <nozzle/backends/opengl.hpp>
#include <nozzle/sender.hpp>
#include <nozzle/frame.hpp>
#include <nozzle/texture.hpp>
#include <nozzle/result.hpp>

#include <cstring>
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
#include <d3d11.h>
#include <windows.h>
#include <GL/gl.h>
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
        case texture_format::rgba16_unorm:
            out = {GL_RGBA16, GL_RGBA, GL_UNSIGNED_SHORT};
            return true;
        case texture_format::r16_float:
            out = {GL_R16F, GL_RED, GL_HALF_FLOAT};
            return true;
        case texture_format::rg16_float:
            out = {GL_RG16F, GL_RG, GL_HALF_FLOAT};
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
        case texture_format::rgba32_float:
            out = {GL_RGBA32F, GL_RGBA, GL_FLOAT};
            return true;
        case texture_format::r32_uint:
            out = {GL_R32UI, GL_RED_INTEGER, GL_UNSIGNED_INT};
            return true;
        case texture_format::rgba32_uint:
            out = {GL_RGBA32UI, GL_RGBA_INTEGER, GL_UNSIGNED_INT};
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

} // anonymous namespace

#if NOZZLE_PLATFORM_MACOS

namespace {

void blit_publish_to_canonical_macos_cgl_iosurface(
    texture_origin src_origin, GLint w, GLint h) {
    if (src_origin == texture_origin::top_left) {
        glBlitFramebuffer(0, 0, w, h, 0, 0, w, h,
                          GL_COLOR_BUFFER_BIT, GL_NEAREST);
    } else {
        glBlitFramebuffer(0, 0, w, h, 0, h, w, 0,
                          GL_COLOR_BUFFER_BIT, GL_NEAREST);
    }
}

void blit_canonical_to_dest_macos_cgl_iosurface(
    texture_origin dst_origin, GLint w, GLint h) {
    if (dst_origin == texture_origin::top_left) {
        glBlitFramebuffer(0, 0, w, h, 0, 0, w, h,
                          GL_COLOR_BUFFER_BIT, GL_NEAREST);
    } else {
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

    // CGL only accepts BGRA for 8-bit IOSurface textures (RGBA → error 10008).
    // Force bgra8_unorm for the writable frame regardless of source format.
    // Since CGL binding always uses GL_BGRA, GL packs colors as BGRA into the
    // BGRA IOSurface — bytes are already correct after blit. No swap needed.
    texture_format publish_format = gl_desc.format;
    if (gl_desc.format == texture_format::rgba8_unorm ||
        gl_desc.format == texture_format::rgba8_srgb) {
        publish_format = texture_format::bgra8_unorm;
    } else if (gl_desc.format == texture_format::bgra8_srgb) {
        publish_format = texture_format::bgra8_unorm;
    }

    nozzle::texture_desc td{};
    td.width = gl_desc.width;
    td.height = gl_desc.height;
    td.format = publish_format;
    td.swizzle = channel_swizzle::identity;

    auto frame_result = snd.acquire_writable_frame(td);
    if (!frame_result) { return frame_result.error(); }

    auto &wframe = frame_result.value();
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

    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glFlush();

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

    ID3D11Texture2D *d3d_tex = d3d11::get_texture(wframe.get_texture());
    if (!d3d_tex) {
        return Error{ErrorCode::BackendError, "no D3D11 texture in writable frame"};
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

    for (uint32_t y = 0; y < gl_desc.height; ++y) {
        std::memcpy(
            static_cast<uint8_t *>(mapped.pData) + y * mapped.RowPitch,
            pixel_data.data() + y * row_bytes,
            row_bytes
        );
    }

    d3d_ctx->Unmap(staging, 0);
    d3d_ctx->CopySubresourceRegion(d3d_tex, 0, 0, 0, 0, staging, 0, nullptr);

    staging->Release();
    d3d_ctx->Release();
    d3d_device->Release();

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

    // TODO: Phase 2 — use EGLImage + GL mapping for zero-copy.
    // Phase 1: CPU copy via pixel access API (once DMA-BUF mmap is implemented).
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

    gl_format_mapping gl_fmt;
    if (!map_format(gl_desc.format, gl_fmt)) {
        return Error{ErrorCode::UnsupportedFormat, "unsupported format for GL interop"};
    }

    void *egl_image = dma_buf::get_egl_image(frm.get_texture());
    if (!egl_image) {
        return Error{ErrorCode::BackendError, "no EGLImage in frame"};
    }

    // TODO: Phase 2 — use EGLImage + GL mapping for zero-copy.
    // Phase 1: CPU copy via pixel access API (once DMA-BUF mmap is implemented).

    return {};
}

#endif // NOZZLE_PLATFORM_LINUX

} // namespace nozzle::gl
