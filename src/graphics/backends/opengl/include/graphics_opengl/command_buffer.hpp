#pragma once
#include "graphics/command_buffer.hpp"
#include "graphics_opengl/command_buffer_commands.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace fei {

class CommandBufferExecutorOpenGL;
class GraphicsDeviceOpenGL;

class CommandBufferOpenGL : public CommandBuffer {
  private:
    friend class CommandBufferExecutorOpenGL;
    friend class GraphicsDeviceOpenGL;

    enum class State {
        Initial,
        Recording,
        Executable,
        Submitted,
    };

    std::vector<opengl_commands::Command> m_commands;
    State m_state {State::Initial};
    const GraphicsDeviceOpenGL& m_device;

  public:
    CommandBufferOpenGL(const GraphicsDeviceOpenGL& device) :
        m_device(device) {}
    void begin() override;
    void end() override;

    void begin_render_pass(const RenderPassDescription& desc) override;
    void end_render_pass() override;

    void set_viewport(
        std::int32_t x,
        std::int32_t y,
        std::uint32_t w,
        std::uint32_t h
    ) override;
    void clear_color(const Color4F& color) override;
    void clear_depth(float depth) override;
    void clear_stencil(std::uint8_t stencil) override;
    void set_vertex_buffer(std::shared_ptr<const Buffer> buffer) override;
    void set_resource_set(
        uint32 slot,
        std::shared_ptr<const ResourceSet> resource_set
    ) override;
    void update_buffer(
        std::shared_ptr<Buffer> buffer,
        const void* data,
        std::size_t size
    ) override;
    void draw(std::size_t start, std::size_t count) override;
    void draw_indexed(std::size_t count) override;
    void dispatch(
        std::size_t group_x,
        std::size_t group_y,
        std::size_t group_z
    ) override;

    void blit_to(std::shared_ptr<const Framebuffer> target) override;

  protected:
    void set_framebuffer_impl(
        std::shared_ptr<const Framebuffer> framebuffer
    ) override;
    void
    set_render_pipeline_impl(std::shared_ptr<const Pipeline> pipeline) override;
    void set_compute_pipeline_impl(
        std::shared_ptr<const Pipeline> pipeline
    ) override;
    void set_index_buffer_impl(
        std::shared_ptr<const Buffer> buffer,
        IndexFormat format,
        uint32 offset
    ) override;
    void generate_mipmaps_impl(std::shared_ptr<const Texture> texture) override;
    void copy_texture_impl(
        std::shared_ptr<const Texture> src,
        uint32 src_x,
        uint32 src_y,
        uint32 src_z,
        uint32 src_mip_level,
        uint32 src_base_array_layer,
        std::shared_ptr<const Texture> dst,
        uint32 dst_x,
        uint32 dst_y,
        uint32 dst_z,
        uint32 dst_mip_level,
        uint32 dst_base_array_layer,
        uint32 width,
        uint32 height,
        uint32 depth,
        uint32 layer_count
    ) override;

  private:
    void ensure_recording(const char* command_name) const;
    void ensure_executable(const char* operation_name) const;
    void mark_submitted();
};

} // namespace fei
