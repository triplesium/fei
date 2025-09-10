#pragma once

#include "graphics/command_buffer.hpp"

#include <cstdint>

namespace fei {

class CommandBufferOpenGL : public CommandBuffer {
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
    virtual void bind_vertex_buffer(std::shared_ptr<Buffer> buffer) override;
    virtual void bind_index_buffer(std::shared_ptr<Buffer> buffer) override;
    virtual void update_buffer(
        std::shared_ptr<Buffer> buffer,
        const void* data,
        std::size_t size
    ) override;
    virtual void draw(std::size_t start, std::size_t count) override;
    virtual void draw_indexed(std::size_t count) override;
    virtual void
    set_uniform(const std::string& name, UniformValue value) override;

  protected:
    virtual void bind_framebuffer_impl(std::shared_ptr<Framebuffer> framebuffer
    ) override;
    virtual void bind_pipeline_impl(std::shared_ptr<Pipeline> pipeline
    ) override;
};

} // namespace fei
