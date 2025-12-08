#pragma once
#include "graphics/enums.hpp"
#include "graphics/resource.hpp"
#include "graphics/shader_module.hpp"
#include "graphics/texture.hpp"
#include "math/matrix.hpp"
#include "math/vector.hpp"

#include <cstdint>
#include <memory>
#include <variant>
#include <vector>

namespace fei {

struct BlendAttachmentDescription {
    bool enabled {false};
    ColorWriteMask color_write_mask {ColorWriteMask::All};
    BlendFactor source_color_factor {BlendFactor::One};
    BlendFactor destination_color_factor {BlendFactor::Zero};
    BlendFunction color_function {BlendFunction::Add};
    BlendFactor source_alpha_factor {BlendFactor::OneMinusSrcAlpha};
    BlendFactor destination_alpha_factor {BlendFactor::Zero};
    BlendFunction alpha_function {BlendFunction::Add};

    static BlendAttachmentDescription OverrideBlend;
    static BlendAttachmentDescription AlphaBlend;
    static BlendAttachmentDescription AdditiveBlend;
    static BlendAttachmentDescription Disabled;
};

inline BlendAttachmentDescription BlendAttachmentDescription::OverrideBlend = {
    true,
    ColorWriteMask::All,
    BlendFactor::One,
    BlendFactor::Zero,
    BlendFunction::Add,
    BlendFactor::One,
    BlendFactor::Zero,
    BlendFunction::Add
};

inline BlendAttachmentDescription BlendAttachmentDescription::AlphaBlend = {
    true,
    ColorWriteMask::All,
    BlendFactor::SrcAlpha,
    BlendFactor::OneMinusSrcAlpha,
    BlendFunction::Add,
    BlendFactor::SrcAlpha,
    BlendFactor::OneMinusSrcAlpha,
    BlendFunction::Add
};

inline BlendAttachmentDescription BlendAttachmentDescription::AdditiveBlend = {
    true,
    ColorWriteMask::All,
    BlendFactor::SrcAlpha,
    BlendFactor::One,
    BlendFunction::Add,
    BlendFactor::SrcAlpha,
    BlendFactor::One,
    BlendFunction::Add
};

inline BlendAttachmentDescription BlendAttachmentDescription::Disabled = {
    false,
    ColorWriteMask::All,
    BlendFactor::One,
    BlendFactor::Zero,
    BlendFunction::Add,
    BlendFactor::One,
    BlendFactor::Zero,
    BlendFunction::Add
};

struct BlendStateDescription {
    std::vector<BlendAttachmentDescription> attachment_states;
    static BlendStateDescription SingleOverrideBlend;
    static BlendStateDescription SingleAlphaBlend;
    static BlendStateDescription SingleAdditiveBlend;
    static BlendStateDescription SingleDisabled;
    static BlendStateDescription Empty;
};

inline BlendStateDescription BlendStateDescription::SingleOverrideBlend = {
    {BlendAttachmentDescription::OverrideBlend}
};

inline BlendStateDescription BlendStateDescription::SingleAlphaBlend = {
    {BlendAttachmentDescription::AlphaBlend}
};

inline BlendStateDescription BlendStateDescription::SingleAdditiveBlend = {
    {BlendAttachmentDescription::AdditiveBlend}
};

inline BlendStateDescription BlendStateDescription::SingleDisabled = {
    {BlendAttachmentDescription::Disabled}
};

inline BlendStateDescription BlendStateDescription::Empty = {{}};

struct StencilBehaviorDescription {
    StencilOperation fail;
    StencilOperation pass;
    StencilOperation depth_fail;
    ComparisonKind comparison;
};

struct DepthStencilStateDescription {
    bool depth_test_enabled {false};
    bool depth_write_enabled {false};
    ComparisonKind depth_comparison {ComparisonKind::Less};
    bool stencil_test_enabled {false};
    StencilBehaviorDescription stencil_front;
    StencilBehaviorDescription stencil_back;
    std::byte stencil_read_mask;
    std::byte stencil_write_mask;
    std::uint32_t stencil_reference {0};

    static DepthStencilStateDescription DepthOnlyLessEqual;
    static DepthStencilStateDescription DepthOnlyLessEqualRead;
    static DepthStencilStateDescription DepthOnlyGreaterEqual;
    static DepthStencilStateDescription DepthOnlyGreaterEqualRead;
    static DepthStencilStateDescription Disabled;
};

inline DepthStencilStateDescription
    DepthStencilStateDescription::DepthOnlyLessEqual = {
        .depth_test_enabled = true,
        .depth_write_enabled = true,
        .depth_comparison = ComparisonKind::LessEqual
};

inline DepthStencilStateDescription
    DepthStencilStateDescription::DepthOnlyLessEqualRead = {
        .depth_test_enabled = true,
        .depth_write_enabled = false,
        .depth_comparison = ComparisonKind::LessEqual
};

inline DepthStencilStateDescription
    DepthStencilStateDescription::DepthOnlyGreaterEqual = {
        .depth_test_enabled = true,
        .depth_write_enabled = true,
        .depth_comparison = ComparisonKind::GreaterEqual
};

inline DepthStencilStateDescription
    DepthStencilStateDescription::DepthOnlyGreaterEqualRead = {
        .depth_test_enabled = true,
        .depth_write_enabled = false,
        .depth_comparison = ComparisonKind::GreaterEqual
};

inline DepthStencilStateDescription DepthStencilStateDescription::Disabled = {
    .depth_test_enabled = false,
    .depth_write_enabled = false,
    .depth_comparison = ComparisonKind::LessEqual
};

struct RasterizerStateDescription {
    CullMode cull_mode {CullMode::Back};
    PolygonFillMode fill_mode {PolygonFillMode::Solid};
    FrontFace front_face {FrontFace::Clockwise};
    bool depth_clip_enabled {true};
    bool scissor_test_enabled {false};
};

struct VertexAttributeDescription {
    std::uint64_t location;
    std::uint64_t offset;
    VertexFormat format;
    bool normalized {false};
};

struct VertexLayoutDescription {
    std::vector<VertexAttributeDescription> attributes {};
    size_t stride {0};
};

struct ShaderProgramDescription {
    std::vector<VertexLayoutDescription> vertex_layouts;
    std::vector<std::shared_ptr<ShaderModule>> shaders;
};

using UniformValue = std::variant<
    float,
    int,
    bool,
    Vector2,
    Vector3,
    Vector4,
    Matrix4x4,
    std::shared_ptr<Texture>>;

struct PipelineDescription {
    BlendStateDescription blend_state;
    DepthStencilStateDescription depth_stencil_state;
    RasterizerStateDescription rasterizer_state;
    RenderPrimitive render_primitive;
    ShaderProgramDescription shader_program;
    // NOTE: really need this?
    std::vector<std::shared_ptr<ResourceLayout>> resource_layouts;
};

class Pipeline {
  public:
    Pipeline(const PipelineDescription& desc) {}
    virtual ~Pipeline() = default;
};
} // namespace fei
