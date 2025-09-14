#pragma once
#include "base/types.hpp"
#include "graphics/buffer.hpp"
#include "graphics/enums.hpp"
#include "graphics/framebuffer.hpp"
#include "graphics/pipeline.hpp"
#include "graphics/resource.hpp"
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
    void set_framebuffer(std::shared_ptr<Framebuffer> framebuffer) {
        m_framebuffer = framebuffer;
        set_framebuffer_impl(framebuffer);
    }
    void set_pipeline(std::shared_ptr<Pipeline> pipeline) {
        m_pipeline = pipeline;
        set_pipeline_impl(pipeline);
    }
    virtual void set_vertex_buffer(std::shared_ptr<Buffer> buffer) = 0;

    virtual void
    set_index_buffer(std::shared_ptr<Buffer> buffer, IndexFormat format) {
        set_index_buffer_impl(buffer, format, 0);
    }

    virtual void set_index_buffer(
        std::shared_ptr<Buffer> buffer,
        IndexFormat format,
        uint32 offset
    ) {
        set_index_buffer_impl(buffer, format, offset);
    }

    virtual void set_resource_set(
        uint32 slot,
        std::shared_ptr<ResourceSet> resource_set
    ) = 0;
    virtual void update_buffer(
        std::shared_ptr<Buffer> buffer,
        const void* data,
        std::size_t size
    ) = 0;
    virtual void draw(std::size_t start, std::size_t count) = 0;
    virtual void draw_indexed(std::size_t count) = 0;

  protected:
    virtual void set_framebuffer_impl(std::shared_ptr<Framebuffer> framebuffer
    ) = 0;
    virtual void set_pipeline_impl(std::shared_ptr<Pipeline> pipeline) = 0;
    virtual void set_index_buffer_impl(
        std::shared_ptr<Buffer> buffer,
        IndexFormat format,
        uint32 offset
    ) = 0;
};

} // namespace fei
