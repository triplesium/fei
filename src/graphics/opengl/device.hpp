#pragma once

#include "graphics/device.hpp"

namespace fei {
class RenderDeviceOpenGL : public RenderDevice {
  public:
    RenderDeviceOpenGL();

    virtual ~RenderDeviceOpenGL() = default;

    virtual Shader*
    create_shader(ShaderStage stage, const std::string& src) override;
    virtual TypeId shader_type() override;

    virtual Program* create_program(
        const Shader& frag_shader,
        const Shader& vert_shader
    ) override;
    virtual TypeId program_type() override;

    virtual Buffer* create_buffer(BufferType type, BufferUsage usage) override;
    virtual TypeId buffer_type() override;

    virtual Texture2D* create_texture2d(const TextureDescriptor& desc) override;
    virtual TypeId texture2d_type() override;

    virtual DrawList* create_draw_list() override;
    virtual TypeId draw_list_type() override;

    virtual RenderPipeline*
    create_render_pipeline(const RenderPipelineDescriptor& desc) override;
    virtual TypeId render_pipeline_type() override;

    virtual Framebuffer* create_framebuffer(const FramebufferDescriptor& desc
    ) override;
    virtual TypeId frame_buffer_type() override;
};

} // namespace fei
