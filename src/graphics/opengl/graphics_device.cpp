#include "graphics_device.hpp"
#include "graphics/opengl/buffer.hpp"
#include "graphics/opengl/command_buffer.hpp"
#include "graphics/opengl/framebuffer.hpp"
#include "graphics/opengl/pipeline.hpp"
#include "graphics/opengl/shader_module.hpp"
#include "graphics/opengl/texture.hpp"
#include "graphics/opengl/utils.hpp"

namespace fei {

GraphicsDeviceOpenGL::GraphicsDeviceOpenGL() {
    GLuint vao;
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
}

std::shared_ptr<ShaderModule>
GraphicsDeviceOpenGL::create_shader_module(const ShaderDescription& desc) {
    return std::make_shared<ShaderOpenGL>(desc);
}

std::shared_ptr<Buffer>
GraphicsDeviceOpenGL::create_buffer(const BufferDescription& desc) {
    return std::make_shared<BufferOpenGL>(desc);
}

std::shared_ptr<Texture>
GraphicsDeviceOpenGL::create_texture(const TextureDescription& desc) {
    return std::make_shared<Texture2DOpenGL>(desc);
}

std::shared_ptr<CommandBuffer> GraphicsDeviceOpenGL::create_command_buffer() {
    return std::make_shared<CommandBufferOpenGL>();
}

std::shared_ptr<Pipeline>
GraphicsDeviceOpenGL::create_render_pipeline(const PipelineDescription& desc) {
    return std::make_shared<PipelineOpenGL>(desc);
}

std::shared_ptr<Framebuffer>
GraphicsDeviceOpenGL::create_framebuffer(const FramebufferDescription& desc) {
    return std::make_shared<FramebufferOpenGL>(desc);
}

void GraphicsDeviceOpenGL::submit_commands(CommandBuffer* command_buffer) {}

void GraphicsDeviceOpenGL::update_texture(
    std::shared_ptr<Texture> texture,
    const void* data,
    std::uint32_t x,
    std::uint32_t y,
    std::uint32_t z,
    std::uint32_t width,
    std::uint32_t height,
    std::uint32_t depth,
    std::uint32_t mip_level,
    std::uint32_t layer
) {
    auto gl_texture = std::static_pointer_cast<Texture2DOpenGL>(texture);

    glBindTexture(GL_TEXTURE_2D, gl_texture->id());
    opengl_check_error();
    glTexSubImage2D(
        GL_TEXTURE_2D,
        mip_level,
        x,
        y,
        width,
        height,
        GL_RGBA,          // This should match texture format
        GL_UNSIGNED_BYTE, // This should match texture data type
        data
    );
    opengl_check_error();
    glBindTexture(GL_TEXTURE_2D, 0);
    opengl_check_error();
}

void GraphicsDeviceOpenGL::update_buffer(
    std::shared_ptr<Buffer> buffer,
    std::uint32_t offset,
    const void* data,
    std::uint32_t size
) {
    auto gl_buffer = std::static_pointer_cast<BufferOpenGL>(buffer);

    glBindBuffer(GL_ARRAY_BUFFER, gl_buffer->id());
    opengl_check_error();
    glBufferSubData(GL_ARRAY_BUFFER, offset, size, data);
    opengl_check_error();
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    opengl_check_error();
}

std::shared_ptr<Framebuffer> GraphicsDeviceOpenGL::main_framebuffer() {
    // Return the default framebuffer (ID 0)
    return std::shared_ptr<FramebufferOpenGL>(new FramebufferOpenGL(0));
}

} // namespace fei
