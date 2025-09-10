#pragma once

#include "graphics/buffer.hpp"
#include "graphics/command_buffer.hpp"
#include "graphics/framebuffer.hpp"
#include "graphics/shader_module.hpp"
#include "graphics/texture.hpp"

#include <cstdint>
#include <memory>

namespace fei {

class GraphicsDevice {
  public:
    virtual ~GraphicsDevice() = default;

    virtual std::shared_ptr<ShaderModule>
    create_shader_module(const ShaderDescription& desc) = 0;
    virtual std::shared_ptr<Buffer> create_buffer(const BufferDescription& desc
    ) = 0;
    virtual std::shared_ptr<Texture>
    create_texture(const TextureDescription& desc) = 0;
    virtual std::shared_ptr<CommandBuffer> create_command_buffer() = 0;
    virtual std::shared_ptr<Pipeline>
    create_render_pipeline(const PipelineDescription& desc) = 0;
    virtual std::shared_ptr<Framebuffer>
    create_framebuffer(const FramebufferDescription& desc) = 0;
    virtual void submit_commands(CommandBuffer* command_buffer) = 0;

    virtual void update_texture(
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
    ) = 0;

    virtual void update_buffer(
        std::shared_ptr<Buffer> buffer,
        std::uint32_t offset,
        const void* data,
        std::uint32_t size
    ) = 0;

    virtual std::shared_ptr<Framebuffer> main_framebuffer() = 0;
};

} // namespace fei
