#pragma once
#include "graphics/enums.hpp"
#include "graphics/opengl/utils.hpp"
#include "graphics/pipeline.hpp"
#include "graphics/shader_module.hpp"

#include <memory>
#include <vector>

namespace fei {

class PipelineOpenGL : public Pipeline {
  private:
    GLuint m_program;
    std::vector<std::shared_ptr<ShaderModule>> m_shaders;
    std::vector<VertexLayoutDescription> m_vertex_layouts;
    BlendStateDescription m_blend_state;
    DepthStencilStateDescription m_depth_stencil_state;
    RasterizerStateDescription m_rasterizer_state;
    RenderPrimitive m_render_primitive;
    std::vector<std::shared_ptr<ResourceLayout>> m_resource_layouts;

  public:
    PipelineOpenGL(const PipelineDescription& desc);

    const auto& vertex_layouts() const { return m_vertex_layouts; }
    const auto& blend_state() const { return m_blend_state; }
    const auto& depth_stencil_state() const { return m_depth_stencil_state; }
    const auto& rasterizer_state() const { return m_rasterizer_state; }
    const auto& render_primitive() const { return m_render_primitive; }
    const auto& resource_layouts() const { return m_resource_layouts; }
    GLuint program() const { return m_program; }
};

} // namespace fei
