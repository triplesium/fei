#pragma once

#include "graphics/buffer.hpp"
#include "graphics/framebuffer.hpp"
#include "graphics/pipeline.hpp"
#include "math/color.hpp"

#include <cstdint>
#include <memory>

namespace fei {

class CommandBuffer {
  protected:
    std::shared_ptr<Framebuffer> m_framebuffer;
    std::shared_ptr<Pipeline> m_pipeline;

  public:
    virtual void begin() = 0;
    virtual void end() = 0;
    virtual void set_viewport(
        std::int32_t x,
        std::int32_t y,
        std::uint32_t w,
        std::uint32_t h
    ) = 0;
    virtual void clear_color(const Color4F& color) = 0;
    virtual void clear_depth(float depth) = 0;
    void bind_framebuffer(std::shared_ptr<Framebuffer> framebuffer) {
        m_framebuffer = framebuffer;
        bind_framebuffer_impl(framebuffer);
    }
    void bind_pipeline(std::shared_ptr<Pipeline> pipeline) {
        m_pipeline = pipeline;
        bind_pipeline_impl(pipeline);
    }
    virtual void bind_vertex_buffer(std::shared_ptr<Buffer> buffer) = 0;
    virtual void bind_index_buffer(std::shared_ptr<Buffer> buffer) = 0;
    virtual void update_buffer(
        std::shared_ptr<Buffer> buffer,
        const void* data,
        std::size_t size
    ) = 0;
    virtual void draw(std::size_t start, std::size_t count) = 0;
    virtual void draw_indexed(std::size_t count) = 0;
    virtual void set_uniform(const std::string& name, UniformValue value) = 0;

  protected:
    virtual void bind_framebuffer_impl(std::shared_ptr<Framebuffer> framebuffer
    ) = 0;
    virtual void bind_pipeline_impl(std::shared_ptr<Pipeline> pipeline) = 0;
};

} // namespace fei
