// nozzle - opengl_backend.cpp - OpenGL↔IOSurface interop via CGLTexImageIOSurface2D

#include <bbb/nozzle/backends/opengl.hpp>
#include <bbb/nozzle/sender.hpp>
#include <bbb/nozzle/frame.hpp>
#include <bbb/nozzle/backends/metal.hpp>
#include <bbb/nozzle/result.hpp>

#if NOZZLE_PLATFORM_MACOS
#define GL_SILENCE_DEPRECATION
#include <OpenGL/gl.h>
#include <OpenGL/OpenGL.h>
#include <IOSurface/IOSurface.h>
#endif

namespace bbb::nozzle::gl {

namespace {

#ifndef GL_RGBA16F
#define GL_RGBA16F 0x881A
#endif

struct gl_format_mapping {
    uint32_t internal_format;
    uint32_t format;
    uint32_t type;
};

bool map_format(texture_format fmt, gl_format_mapping &out) {
    switch (fmt) {
        case texture_format::rgba8_unorm:
            out = {GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE};
            return true;
        case texture_format::bgra8_unorm:
            out = {GL_RGBA8, GL_BGRA, GL_UNSIGNED_BYTE};
            return true;
        case texture_format::rgba16_float:
            out = {GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT};
            return true;
        default:
            return false;
    }
}

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

    auto frame_result = snd.acquire_writable_frame({
        .width = gl_desc.width,
        .height = gl_desc.height,
        .format = gl_desc.format
    });
    if (!frame_result) { return frame_result.error(); }

    auto &wframe = frame_result.value();
    void *surface_ptr = metal::get_io_surface(wframe.get_texture());
    if (!surface_ptr) {
        return Error{ErrorCode::BackendError, "no IOSurface in writable frame"};
    }

    IOSurfaceRef surface = static_cast<IOSurfaceRef>(surface_ptr);

    scoped_texture io_tex;
    glBindTexture(GL_TEXTURE_2D, io_tex.name);

    CGLError cgl_err = CGLTexImageIOSurface2D(
        CGLGetCurrentContext(),
        GL_TEXTURE_2D,
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
                           gl_desc.target, gl_desc.name, 0);

    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbos.draw_fbo);
    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, io_tex.name, 0);

    glBlitFramebuffer(
        0, 0, static_cast<GLint>(gl_desc.width), static_cast<GLint>(gl_desc.height),
        0, static_cast<GLint>(gl_desc.height), static_cast<GLint>(gl_desc.width), 0,
        GL_COLOR_BUFFER_BIT, GL_NEAREST
    );

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
    glBindTexture(GL_TEXTURE_2D, io_tex.name);

    CGLError cgl_err = CGLTexImageIOSurface2D(
        CGLGetCurrentContext(),
        GL_TEXTURE_2D,
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
                           GL_TEXTURE_2D, io_tex.name, 0);

    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbos.draw_fbo);
    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           gl_desc.target, gl_desc.name, 0);

    glBlitFramebuffer(
        0, static_cast<GLint>(gl_desc.height), static_cast<GLint>(gl_desc.width), 0,
        0, 0, static_cast<GLint>(gl_desc.width), static_cast<GLint>(gl_desc.height),
        GL_COLOR_BUFFER_BIT, GL_NEAREST
    );

    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);

    return {};
}

} // namespace bbb::nozzle::gl
