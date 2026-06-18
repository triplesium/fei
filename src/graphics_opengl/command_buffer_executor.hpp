#pragma once

#include "base/types.hpp"
#include "graphics/buffer.hpp"
#include "graphics/framebuffer.hpp"
#include "graphics/pipeline.hpp"
#include "graphics/resource.hpp"
#include "graphics/texture.hpp"
#include "graphics_opengl/command_buffer.hpp"
#include "graphics_opengl/command_buffer_commands.hpp"

#include <cstddef>
#include <memory>
#include <vector>

namespace fei {

class GraphicsDeviceOpenGL;
class PipelineOpenGL;

class CommandBufferExecutorOpenGL {
  private:
    struct ExecutionState;

    GraphicsDeviceOpenGL& m_device;

  public:
    explicit CommandBufferExecutorOpenGL(GraphicsDeviceOpenGL& device) :
        m_device(device) {}

    void execute(CommandBufferOpenGL& command_buffer);

  private:
    void execute_command(
        ExecutionState& state,
        const opengl_commands::Command& command
    );
    void execute_begin_render_pass(
        ExecutionState& state,
        const RenderPassDescription& desc
    );
    void execute_set_framebuffer(
        ExecutionState& state,
        std::shared_ptr<Framebuffer> framebuffer
    );
    void execute_set_render_pipeline(
        ExecutionState& state,
        std::shared_ptr<Pipeline> pipeline
    );
    void execute_set_compute_pipeline(
        ExecutionState& state,
        std::shared_ptr<Pipeline> pipeline
    );
    void execute_set_vertex_buffer(
        ExecutionState& state,
        std::shared_ptr<Buffer> buffer
    );
    void execute_set_resource_set(
        ExecutionState& state,
        uint32 slot,
        std::shared_ptr<ResourceSet> resource_set
    );
    void execute_update_buffer(
        std::shared_ptr<Buffer> buffer,
        const std::vector<std::byte>& data
    );
    void
    execute_draw(ExecutionState& state, std::size_t start, std::size_t count);
    void execute_draw_indexed(ExecutionState& state, std::size_t count);
    void execute_dispatch(
        std::size_t group_x,
        std::size_t group_y,
        std::size_t group_z
    );
    void
    execute_blit_to(ExecutionState& state, std::shared_ptr<Framebuffer> target);
    void execute_generate_mipmaps(std::shared_ptr<Texture> texture);
    void execute_copy_texture(const opengl_commands::CopyTexture& command);
};

} // namespace fei
