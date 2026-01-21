#pragma once
#include "base/log.hpp"
#include "base/optional.hpp"
#include "base/types.hpp"
#include "graphics/enums.hpp"
#include "graphics/opengl/utils.hpp"
#include "graphics/pipeline.hpp"
#include "graphics/shader_module.hpp"

#include <memory>
#include <variant>
#include <vector>

namespace fei {

class PipelineOpenGL : public Pipeline {
  public:
    struct TextureBinding {
        GLuint unit;
        GLint location;
    };

    struct SamplerBinding {
        std::vector<GLuint> units;
    };

    struct UniformBinding {
        GLuint location;
    };

    struct EmptyBinding {};

    using ResourceBindingInfo = std::
        variant<TextureBinding, SamplerBinding, UniformBinding, EmptyBinding>;

  private:
    GLuint m_program;
    std::vector<std::shared_ptr<ShaderModule>> m_shaders;
    std::vector<VertexLayoutDescription> m_vertex_layouts;
    BlendStateDescription m_blend_state;
    DepthStencilStateDescription m_depth_stencil_state;
    RasterizerStateDescription m_rasterizer_state;
    RenderPrimitive m_render_primitive;
    std::vector<std::shared_ptr<ResourceLayout>> m_resource_layouts;

    std::vector<std::vector<ResourceBindingInfo>> m_resource_bindings;

  public:
    PipelineOpenGL(const PipelineDescription& desc);

    const auto& vertex_layouts() const { return m_vertex_layouts; }
    const auto& blend_state() const { return m_blend_state; }
    const auto& depth_stencil_state() const { return m_depth_stencil_state; }
    const auto& rasterizer_state() const { return m_rasterizer_state; }
    const auto& render_primitive() const { return m_render_primitive; }
    const auto& resource_layouts() const { return m_resource_layouts; }
    GLuint program() const { return m_program; }

    uint32 uniform_buffer_count(uint32 slot) const {
        uint32 count = 0;
        for (const auto& binding : m_resource_bindings[slot]) {
            if (std::holds_alternative<UniformBinding>(binding)) {
                count++;
            }
        }
        return count;
    }

    Optional<ResourceBindingInfo&>
    get_resource_binding(uint32 slot, uint32 index) {
        if (slot >= m_resource_bindings.size()) {
            fei::error(
                "Resource binding slot {} out of range (max {})",
                slot,
                m_resource_bindings.size()
            );
            return nullopt;
        }
        auto& bindings = m_resource_bindings[slot];
        if (index >= bindings.size()) {
            fei::error(
                "Resource binding index {} out of range (max {})",
                index,
                bindings.size()
            );
            return nullopt;
        }
        return bindings[index];
    }

    void process_resource_layouts();
};

} // namespace fei
