#include "device.hpp"
#include "graphics/opengl/buffer.hpp"
#include "graphics/opengl/draw_list.hpp"
#include "graphics/opengl/framebuffer.hpp"
#include "graphics/opengl/program.hpp"
#include "graphics/opengl/render_pipeline.hpp"
#include "graphics/opengl/shader.hpp"
#include "graphics/opengl/texture2d.hpp"
#include "graphics/opengl/utils.hpp"

namespace fei {

RenderDeviceOpenGL::RenderDeviceOpenGL() {
    GLuint vao;
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
}

Shader*
RenderDeviceOpenGL::create_shader(ShaderStage stage, const std::string& src) {
    return new ShaderOpenGL(stage, src);
}

TypeId RenderDeviceOpenGL::shader_type() {
    return type_id<ShaderOpenGL>();
}

Program* RenderDeviceOpenGL::create_program(
    const Shader& frag_shader,
    const Shader& vert_shader
) {
    return new ProgramOpenGL(frag_shader, vert_shader);
}

TypeId RenderDeviceOpenGL::program_type() {
    return type_id<ProgramOpenGL>();
}

Buffer* RenderDeviceOpenGL::create_buffer(BufferType type, BufferUsage usage) {
    return new BufferOpenGL(type, usage);
}

TypeId RenderDeviceOpenGL::buffer_type() {
    return type_id<BufferOpenGL>();
}

Texture2D* RenderDeviceOpenGL::create_texture2d(const TextureDescriptor& desc) {
    return new Texture2DOpenGL(desc);
}

TypeId RenderDeviceOpenGL::texture2d_type() {
    return type_id<Texture2DOpenGL>();
}

DrawList* RenderDeviceOpenGL::create_draw_list() {
    return new DrawListOpenGL();
}

TypeId RenderDeviceOpenGL::draw_list_type() {
    return type_id<DrawListOpenGL>();
}

RenderPipeline*
RenderDeviceOpenGL::create_render_pipeline(const RenderPipelineDescriptor& desc
) {
    return new RenderPipelineOpenGL(desc);
}

TypeId RenderDeviceOpenGL::render_pipeline_type() {
    return type_id<RenderPipelineOpenGL>();
}

Framebuffer*
RenderDeviceOpenGL::create_framebuffer(const FramebufferDescriptor& desc) {
    return new FramebufferOpenGL(desc);
}

TypeId RenderDeviceOpenGL::frame_buffer_type() {
    return type_id<FramebufferOpenGL>();
}

} // namespace fei
