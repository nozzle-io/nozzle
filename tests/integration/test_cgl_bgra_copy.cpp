#define GL_SILENCE_DEPRECATION
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl3.h>
#include <catch2/catch_test_macros.hpp>
#include <nozzle/backends/opengl.hpp>
#include <nozzle/receiver.hpp>

#include <array>
#include <string>
#include <unistd.h>

namespace {

struct cgl_fixture {
    CGLContextObj previous{CGLGetCurrentContext()};
    CGLContextObj context{};
    GLuint textures[2]{};

    cgl_fixture() {
        if (previous) CGLRetainContext(previous);
    }

    ~cgl_fixture() {
        if (context) {
            if (context == CGLGetCurrentContext()) glDeleteTextures(2, textures);
            CGLSetCurrentContext(previous);
            CGLReleaseContext(context);
        }
        if (previous) CGLReleaseContext(previous);
    }
};

} // namespace

TEST_CASE("CGL copies a BGRA IOSurface into RGBA8 GL storage", "[.][cgl_bgra_copy]") {
    cgl_fixture gl;
    const CGLPixelFormatAttribute attributes[]{kCGLPFAOpenGLProfile,
        static_cast<CGLPixelFormatAttribute>(kCGLOGLPVersion_GL4_Core), static_cast<CGLPixelFormatAttribute>(0)};
    CGLPixelFormatObj format{};
    GLint count{};
    REQUIRE(CGLChoosePixelFormat(attributes, &format, &count) == kCGLNoError);
    REQUIRE(format != nullptr);
    const auto created = CGLCreateContext(format, nullptr, &gl.context);
    CGLReleasePixelFormat(format);
    REQUIRE(created == kCGLNoError);
    REQUIRE(CGLSetCurrentContext(gl.context) == kCGLNoError);

    const std::array<unsigned char, 8> expected{219, 53, 17, 255, 11, 105, 193, 255};
    glGenTextures(2, gl.textures);
    for (const GLuint name : gl.textures) {
        glBindTexture(GL_TEXTURE_2D, name);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 2, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE,
            name == gl.textures[0] ? expected.data() : nullptr);
    }
    REQUIRE(glGetError() == GL_NO_ERROR);

    nozzle::sender_desc sender_desc{};
    sender_desc.name = "cgl-bgra-" + std::to_string(getpid());
    auto sender = nozzle::sender::create(sender_desc);
    REQUIRE(sender.ok());
    nozzle::gl::gl_texture_desc transfer{};
    transfer.name = gl.textures[0];
    transfer.width = 2;
    transfer.height = 1;
    REQUIRE(nozzle::gl::publish_gl_texture(sender.value(), transfer).ok());
    glFinish();

    nozzle::receiver_desc receiver_desc{};
    receiver_desc.name = sender_desc.name;
    auto receiver = nozzle::receiver::create(receiver_desc);
    REQUIRE(receiver.ok());
    auto frame = receiver.value().acquire_frame();
    REQUIRE(frame.ok());
    REQUIRE(frame.value().info().format == nozzle::texture_format::bgra8_unorm);
    REQUIRE(frame.value().info().semantic_format == nozzle::texture_format::rgba8_unorm);

    // Both descriptions refer to the same BGRA source and the same RGBA8 destination.
    transfer.name = gl.textures[1];
    for (const auto format_hint : {nozzle::texture_format::rgba8_unorm, nozzle::texture_format::bgra8_unorm}) {
        CAPTURE(format_hint);
        transfer.format = format_hint;
        std::array<unsigned char, 8> pixels{};
        glBindTexture(GL_TEXTURE_2D, transfer.name);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 2, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        auto copy = nozzle::gl::copy_frame_to_gl_texture(frame.value(), transfer);
        if (!copy.ok()) {
            FAIL(copy.error().message);
        }
        glBindTexture(GL_TEXTURE_2D, transfer.name);
        glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        REQUIRE(glGetError() == GL_NO_ERROR);
        REQUIRE(pixels == expected);
    }
}
