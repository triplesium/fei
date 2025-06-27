#pragma once

#include "graphics/enums.hpp"
#include "graphics/program.hpp"
#include "math/matrix.hpp"
#include "math/vector.hpp"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace fei {
struct VertexAttribute {
    std::uint32_t location;
    std::uint32_t offset;
    VertexFormat format;
    bool normalized {false};
};

struct VertexLayout {
    std::vector<VertexAttribute> attributes {};
    size_t stride {0};
};

struct RenderPipelineRasterizationState {
    bool wireframe {false};
    CullMode cull_mode {CullMode::Back};
    FrontFace front_face {FrontFace::CounterClockwise};
    float line_width {1.0};
};

using UniformValue =
    std::variant<float, int, bool, Vector2, Vector3, Vector4, Matrix4x4>;

struct RenderPipelineDescriptor {
    Program* program;
    std::unordered_map<std::string, UniformValue> uniforms;
    VertexLayout vertex_layout;
    RenderPrimitive render_primitive;
    RenderPipelineRasterizationState rasterization_state;
};

class RenderPipeline {
  public:
    RenderPipeline(const RenderPipelineDescriptor& desc) :
        m_program(desc.program), m_uniforms(desc.uniforms),
        m_vertex_layout(desc.vertex_layout),
        m_render_primitive(desc.render_primitive) {}
    virtual ~RenderPipeline() = default;

    Program* program() const { return m_program; }
    void set_program(Program* p) { m_program = p; }

    const VertexLayout& vertex_layout() const { return m_vertex_layout; }
    RenderPrimitive render_primitive() const { return m_render_primitive; }
    const std::unordered_map<std::string, UniformValue>& uniforms() const {
        return m_uniforms;
    }
    void set_uniform(const std::string& name, const UniformValue& value) {
        m_uniforms[name] = value;
    }

  private:
    Program* m_program;
    std::unordered_map<std::string, UniformValue> m_uniforms;
    VertexLayout m_vertex_layout;
    RenderPrimitive m_render_primitive;
};
} // namespace fei
