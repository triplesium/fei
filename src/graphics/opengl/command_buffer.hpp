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
    std::shared_ptr<GraphicsDeviceOpenGL> m_device;
    GLenum m_draw_elements_type;

  public:
    virtual void begin() override;
    virtual void end() override;
    virtual void set_viewport(
        std::int32_t x,
        std::int32_t y,
        std::uint32_t w,
        std::uint32_t h
    ) override;
    virtual void clear_color(const Color4F& color) override;
    virtual void clear_depth(float depth) override;
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

  protected:
    virtual void set_framebuffer_impl(std::shared_ptr<Framebuffer> framebuffer
    ) override;
    virtual void set_pipeline_impl(std::shared_ptr<Pipeline> pipeline) override;
    virtual void set_index_buffer_impl(
        std::shared_ptr<Buffer> buffer,
        IndexFormat format,
        uint32 offset
    ) override;
};

} // namespace fei
