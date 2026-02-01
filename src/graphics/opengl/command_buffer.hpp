#pragma once
#include "graphics/command_buffer.hpp"
#include "graphics/opengl/graphics_device.hpp"
#include "graphics/opengl/resource.hpp"
#include "graphics/opengl/utils.hpp"
#include "graphics/resource.hpp"

#include <cstdint>
#include <memory>
#include <vector>

namespace fei {

class CommandBufferOpenGL : public CommandBuffer {
  private:
    std::vector<std::shared_ptr<ResourceSetOpenGL>> m_bound_resource_sets;
    GraphicsDeviceOpenGL& m_device;
    GLenum m_draw_elements_type;

  public:
    CommandBufferOpenGL(GraphicsDeviceOpenGL& device) : m_device(device) {}
    virtual void begin() override;
    virtual void end() override;

    virtual void begin_render_pass(const RenderPassDescription& desc) override;
    virtual void end_render_pass() override;

    virtual void set_viewport(
        std::int32_t x,
        std::int32_t y,
        std::uint32_t w,
        std::uint32_t h
    ) override;
    virtual void clear_color(const Color4F& color) override;
    virtual void clear_depth(float depth) override;
    virtual void clear_stencil(std::uint8_t stencil) override;
    virtual void set_vertex_buffer(std::shared_ptr<Buffer> buffer) override;
    virtual void set_resource_set(
        uint32 slot,
        std::shared_ptr<ResourceSet> resource_set
    ) override;
    virtual void update_buffer(
        std::shared_ptr<Buffer> buffer,
        const void* data,
        std::size_t size
    ) override;
    virtual void draw(std::size_t start, std::size_t count) override;
    virtual void draw_indexed(std::size_t count) override;
    virtual void
    dispatch(std::size_t group_x, std::size_t group_y, std::size_t group_z)
        override;

    virtual void blit_to(std::shared_ptr<Framebuffer> target) override;

  protected:
    virtual void set_framebuffer_impl(std::shared_ptr<Framebuffer> framebuffer
    ) override;
    virtual void set_render_pipeline_impl(std::shared_ptr<Pipeline> pipeline
    ) override;
    virtual void set_compute_pipeline_impl(std::shared_ptr<Pipeline> pipeline
    ) override;
    virtual void set_index_buffer_impl(
        std::shared_ptr<Buffer> buffer,
        IndexFormat format,
        uint32 offset
    ) override;

  private:
    uint32 calculate_uniform_block_base_index(uint32 slot);
};

} // namespace fei
