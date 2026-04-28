#pragma once

#if !defined(NOZZLE_HAS_OPENGL)
#error "OpenGL interop not available — enable NOZZLE_BUILD_OPENGL in CMake"
#endif

#include <bbb/nozzle/types.hpp>
#include <bbb/nozzle/result.hpp>
#include <bbb/nozzle/sender.hpp>
#include <bbb/nozzle/frame.hpp>

namespace bbb::nozzle::gl {

struct gl_texture_desc {
    uint32_t name{0};           // GL texture name (GLuint)
    uint32_t target{0x0DE1};    // GL_TEXTURE_2D
    uint32_t width{0};
    uint32_t height{0};
    texture_format format{texture_format::rgba8_unorm};
};

// Copy GL texture data to sender's shared texture and commit.
// Requires active GL context on calling thread.
// This is a GPU→CPU→GPU copy path.
Result<void> publish_gl_texture(sender &snd, const gl_texture_desc &gl_desc);

// Copy frame data to GL texture (receiver side).
// Requires active GL context on calling thread.
// This is a GPU→CPU→GPU copy path.
Result<void> copy_frame_to_gl_texture(const frame &frm, const gl_texture_desc &gl_desc);

} // namespace bbb::nozzle::gl
