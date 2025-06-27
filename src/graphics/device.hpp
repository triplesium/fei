#pragma once

#include "graphics/buffer.hpp"
#include "graphics/draw_list.hpp"
#include "graphics/enums.hpp"
#include "graphics/program.hpp"
#include "graphics/shader.hpp"
#include "graphics/texture2d.hpp"
#include "refl/type.hpp"
namespace fei {

class RenderDevice {
  public:
    static RenderDevice* s_instance;
    static void set_instance(RenderDevice* instance) { s_instance = instance; }
    static RenderDevice* instance() { return s_instance; }

    virtual ~RenderDevice() = default;

    virtual Shader*
    create_shader(ShaderStage stage, const std::string& src) = 0;
    virtual TypeId shader_type() = 0;

    virtual Program*
    create_program(const Shader& frag_shader, const Shader& vert_shader) = 0;
    virtual TypeId program_type() = 0;

    virtual Buffer* create_buffer(BufferType type, BufferUsage usage) = 0;
    virtual TypeId buffer_type() = 0;

    virtual Texture2D* create_texture2d(const TextureDescriptor& desc) = 0;
    virtual TypeId texture2d_type() = 0;

    virtual DrawList* create_draw_list() = 0;
    virtual TypeId draw_list_type() = 0;

    virtual RenderPipeline*
    create_render_pipeline(const RenderPipelineDescriptor& desc) = 0;
    virtual TypeId render_pipeline_type() = 0;

    virtual Framebuffer* create_framebuffer(const FramebufferDescriptor& desc
    ) = 0;
    virtual TypeId frame_buffer_type() = 0;
};

} // namespace fei
