#pragma once

#include "graphics/graphics_device.hpp"
#include "graphics/shader_module.hpp"

namespace fei {
class GraphicsDeviceOpenGL : public GraphicsDevice {
  public:
    GraphicsDeviceOpenGL();
    virtual ~GraphicsDeviceOpenGL() = default;

    virtual std::shared_ptr<ShaderModule>
    create_shader_module(const ShaderDescription& desc) override;
    virtual std::shared_ptr<Buffer> create_buffer(const BufferDescription& desc
    ) override;
    virtual std::shared_ptr<Texture>
    create_texture(const TextureDescription& desc) override;
    virtual std::shared_ptr<CommandBuffer> create_command_buffer() override;
    virtual std::shared_ptr<Pipeline>
    create_render_pipeline(const PipelineDescription& desc) override;
    virtual std::shared_ptr<Framebuffer>
    create_framebuffer(const FramebufferDescription& desc) override;
    virtual void submit_commands(CommandBuffer* command_buffer) override;

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
    ) override;

    virtual void update_buffer(
        std::shared_ptr<Buffer> buffer,
        std::uint32_t offset,
        const void* data,
        std::uint32_t size
    ) override;

    std::shared_ptr<Framebuffer> main_framebuffer() override;
};

} // namespace fei
