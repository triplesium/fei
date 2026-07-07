#include "pbr/pipelines.hpp"

#include "../../rendering/tests/test_graphics_device.hpp"
#include "asset/assets.hpp"
#include "asset/server.hpp"
#include "base/optional.hpp"
#include "graphics/enums.hpp"
#include "graphics/pipeline.hpp"
#include "graphics/resource.hpp"
#include "graphics/shader_module.hpp"
#include "pbr/mesh_view.hpp"
#include "pbr/pipeline_specializer.hpp"
#include "rendering/material.hpp"
#include "rendering/mesh/mesh.hpp"
#include "rendering/shader.hpp"
#include "rendering/shader_cache.hpp"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

using namespace fei;
using namespace fei::rendering_test;

// NOLINTBEGIN(clang-analyzer-cplusplus.NewDeleteLeaks)

static_assert(std::is_same_v<
              decltype(std::declval<PreparedMaterial&>().resource_layout()),
              std::shared_ptr<ResourceLayout>>);
static_assert(
    std::is_same_v<
        decltype(std::declval<const PreparedMaterial&>().resource_layout()),
        std::shared_ptr<const ResourceLayout>>
);
static_assert(std::is_same_v<
              decltype(std::declval<PreparedMaterial&>().resource_set()),
              std::shared_ptr<ResourceSet>>);
static_assert(std::is_same_v<
              decltype(std::declval<const PreparedMaterial&>().resource_set()),
              std::shared_ptr<const ResourceSet>>);
static_assert(std::is_same_v<
              decltype(std::declval<PreparedMaterial&>()
                           .shader_request(MaterialShaderType::Vertex)),
              Optional<PreparedMaterialShader&>>);
static_assert(std::is_same_v<
              decltype(std::declval<const PreparedMaterial&>()
                           .shader_request(MaterialShaderType::Vertex)),
              Optional<const PreparedMaterialShader&>>);

namespace {

class TestPipelineSpecializer : public PipelineSpecializer {
  private:
    std::size_t m_cache_key {0};
    CullMode m_cull_mode {CullMode::Back};
    BitFlags<PbrMeshPipelineKeyFlags> m_flags {
        PbrMeshPipelineKeyFlags::MeshPipeline
    };

  public:
    TestPipelineSpecializer(std::size_t cache_key, CullMode cull_mode) :
        m_cache_key(cache_key), m_cull_mode(cull_mode) {}

    TestPipelineSpecializer(
        std::size_t cache_key,
        CullMode cull_mode,
        BitFlags<PbrMeshPipelineKeyFlags> flags
    ) : m_cache_key(cache_key), m_cull_mode(cull_mode), m_flags(flags) {}

    std::size_t cache_key() const override { return m_cache_key; }

    BitFlags<PbrMeshPipelineKeyFlags> mesh_pipeline_flags() const override {
        return m_flags;
    }

    void specialize(
        RenderPipelineDescription& desc,
        const GpuMesh&,
        const PreparedMaterial&
    ) const override {
        desc.rasterizer_state.cull_mode = m_cull_mode;
    }
};

bool has_shader_def(const ShaderDefs& defs, std::string_view name) {
    return std::any_of(defs.begin(), defs.end(), [&](const ShaderDefVal& def) {
        return def.name == name && def.value == ShaderDefValue {true};
    });
}

bool has_vertex_attribute(
    const VertexLayoutDescription& layout,
    MeshVertexAttributeId location
) {
    return std::any_of(
        layout.attributes.begin(),
        layout.attributes.end(),
        [&](const VertexAttributeDescription& attribute) {
            return attribute.location == location;
        }
    );
}

std::shared_ptr<ResourceLayout> create_layout(FakeGraphicsDevice& device) {
    return device.create_resource_layout(ResourceLayoutDescription {});
}

PbrMeshShaderDefaults create_shader_defaults(FakeGraphicsDevice& device) {
    return PbrMeshShaderDefaults {
        .forward_vertex = device.create_shader_module(
            ShaderDescription {.stage = ShaderStages::Vertex}
        ),
        .forward_fragment = device.create_shader_module(
            ShaderDescription {.stage = ShaderStages::Fragment}
        ),
        .prepass_vertex = device.create_shader_module(
            ShaderDescription {.stage = ShaderStages::Vertex}
        ),
        .prepass_fragment = device.create_shader_module(
            ShaderDescription {.stage = ShaderStages::Fragment}
        ),
    };
}

PreparedMaterial create_material(
    FakeGraphicsDevice& device,
    std::shared_ptr<ResourceLayout> layout,
    std::size_t material_hash = 1,
    MaterialPipelineState pipeline_state = {}
) {
    hash_combine(material_hash, pipeline_state);
    auto resource_set = device.create_resource_set(
        ResourceSetDescription {
            .layout = layout,
            .resources = {},
        }
    );
    return PreparedMaterial {
        {},
        std::move(layout),
        std::move(resource_set),
        material_hash,
        pipeline_state
    };
}

PreparedMaterial create_default_shader_material(
    FakeGraphicsDevice& device,
    std::shared_ptr<ResourceLayout> layout,
    std::size_t material_hash = 1
) {
    auto resource_set = device.create_resource_set(
        ResourceSetDescription {
            .layout = layout,
            .resources = {},
        }
    );
    return PreparedMaterial {
        {},
        std::move(layout),
        std::move(resource_set),
        material_hash
    };
}

Handle<Shader>
add_shader(Assets<Shader>& shaders, ShaderStages stage, std::string path) {
    return shaders.add(
        std::make_unique<Shader>(Shader {
            .path = std::move(path),
            .source = {},
            .spirv = {},
            .stage = stage,
            .resources = {},
        })
    );
}

PreparedMaterial create_shader_request_material(
    FakeGraphicsDevice& device,
    std::shared_ptr<ResourceLayout> layout,
    Handle<Shader> vertex_shader,
    Handle<Shader> fragment_shader,
    ShaderDefs fragment_defs,
    MaterialPipelineState pipeline_state = {}
) {
    std::unordered_map<MaterialShaderType, PreparedMaterialShader> shaders {
        {MaterialShaderType::Vertex,
         PreparedMaterialShader {.ref = ShaderRef(vertex_shader), .defs = {}}},
        {MaterialShaderType::Fragment,
         PreparedMaterialShader {
             .ref = ShaderRef(fragment_shader),
             .defs = normalized_shader_defs(std::move(fragment_defs)),
         }},
    };

    auto resource_set = device.create_resource_set(
        ResourceSetDescription {
            .layout = layout,
            .resources = {},
        }
    );
    std::size_t material_hash = 9;
    hash_combine(material_hash, pipeline_state);
    return PreparedMaterial {
        std::move(shaders),
        std::move(layout),
        std::move(resource_set),
        material_hash,
        pipeline_state
    };
}

MeshVertexBufferLayout
create_vertex_layout(std::vector<MeshVertexAttribute> attributes) {
    std::vector<MeshVertexAttributeId> attribute_ids;
    std::vector<VertexAttributeDescription> vertex_attributes;
    std::uint64_t offset = 0;
    for (const auto& attribute : attributes) {
        attribute_ids.push_back(attribute.id);
        vertex_attributes.push_back(
            VertexAttributeDescription {
                .location = attribute.id,
                .offset = offset,
                .format = attribute.format,
            }
        );
        offset += vertex_format_size(attribute.format);
    }

    return MeshVertexBufferLayout {
        .attribute_ids = std::move(attribute_ids),
        .layout = VertexBufferLayout(
            offset,
            VertexStepMode::Vertex,
            std::move(vertex_attributes)
        )
    };
}

GpuMesh create_gpu_mesh(
    RenderPrimitive primitive,
    std::vector<MeshVertexAttribute> attributes = {Mesh::ATTRIBUTE_POSITION}
) {
    auto vertex_layout = create_vertex_layout(std::move(attributes));

    return GpuMesh {
        nullptr,
        nullopt,
        primitive,
        std::move(vertex_layout),
        0,
        3,
    };
}

MeshMaterialPipelines create_mesh_material_pipelines(
    MeshViewLayout& mesh_view_layout,
    MeshUniforms& mesh_uniforms,
    PipelineCache& pipeline_cache,
    ShaderCache& shader_cache,
    const PbrMeshShaderDefaults& shader_defaults
) {
    return MeshMaterialPipelines {
        mesh_view_layout,
        mesh_uniforms,
        pipeline_cache,
        shader_cache,
        shader_defaults,
    };
}

} // namespace

TEST_CASE(
    "MeshMaterialPipelines reuses matching specialized pipeline keys",
    "[pbr][pipelines]"
) {
    FakeGraphicsDevice device;
    MeshViewLayout mesh_view_layout {.layout = create_layout(device)};
    MeshUniforms mesh_uniforms {.resource_layout = create_layout(device)};
    PipelineCache pipeline_cache(device);
    AssetServer asset_server(nullptr);
    Assets<Shader> shaders(nullptr);
    ShaderCache shader_cache(asset_server, shaders, device);
    auto shader_defaults = create_shader_defaults(device);
    auto pipelines = create_mesh_material_pipelines(
        mesh_view_layout,
        mesh_uniforms,
        pipeline_cache,
        shader_cache,
        shader_defaults
    );

    auto material = create_material(device, create_layout(device));
    auto mesh = create_gpu_mesh(RenderPrimitive::Triangles);
    TestPipelineSpecializer specializer {11, CullMode::Back};

    auto first_id = pipelines.request(1, material, mesh, specializer);
    auto second_id = pipelines.request(2, material, mesh, specializer);

    REQUIRE(first_id == second_id);
    auto found_id = pipelines.find(1, material, mesh, specializer);
    REQUIRE(found_id);
    REQUIRE(*found_id == first_id);
    REQUIRE(device.render_pipeline_descriptions.empty());

    pipeline_cache.process_queued_pipelines();

    REQUIRE(device.render_pipeline_descriptions.size() == 1);
    CHECK(
        device.render_pipeline_descriptions[0].render_primitive ==
        RenderPrimitive::Triangles
    );
}

TEST_CASE(
    "MeshMaterialPipelines separates primitive variants",
    "[pbr][pipelines]"
) {
    FakeGraphicsDevice device;
    MeshViewLayout mesh_view_layout {.layout = create_layout(device)};
    MeshUniforms mesh_uniforms {.resource_layout = create_layout(device)};
    PipelineCache pipeline_cache(device);
    AssetServer asset_server(nullptr);
    Assets<Shader> shaders(nullptr);
    ShaderCache shader_cache(asset_server, shaders, device);
    auto shader_defaults = create_shader_defaults(device);
    auto pipelines = create_mesh_material_pipelines(
        mesh_view_layout,
        mesh_uniforms,
        pipeline_cache,
        shader_cache,
        shader_defaults
    );

    auto material = create_material(device, create_layout(device));
    TestPipelineSpecializer specializer {11, CullMode::Back};

    auto triangle_id = pipelines.request(
        1,
        material,
        create_gpu_mesh(RenderPrimitive::Triangles),
        specializer
    );
    auto line_id = pipelines.request(
        2,
        material,
        create_gpu_mesh(RenderPrimitive::Lines),
        specializer
    );

    REQUIRE(triangle_id != line_id);
    REQUIRE(device.render_pipeline_descriptions.empty());

    pipeline_cache.process_queued_pipelines();

    REQUIRE(device.render_pipeline_descriptions.size() == 2);
    CHECK(
        device.render_pipeline_descriptions[0].render_primitive ==
        RenderPrimitive::Triangles
    );
    CHECK(
        device.render_pipeline_descriptions[1].render_primitive ==
        RenderPrimitive::Lines
    );
}

TEST_CASE(
    "MeshMaterialPipelines separates stateful specializer variants",
    "[pbr][pipelines]"
) {
    FakeGraphicsDevice device;
    MeshViewLayout mesh_view_layout {.layout = create_layout(device)};
    MeshUniforms mesh_uniforms {.resource_layout = create_layout(device)};
    PipelineCache pipeline_cache(device);
    AssetServer asset_server(nullptr);
    Assets<Shader> shaders(nullptr);
    ShaderCache shader_cache(asset_server, shaders, device);
    auto shader_defaults = create_shader_defaults(device);
    auto pipelines = create_mesh_material_pipelines(
        mesh_view_layout,
        mesh_uniforms,
        pipeline_cache,
        shader_cache,
        shader_defaults
    );

    auto material = create_material(device, create_layout(device));
    auto mesh = create_gpu_mesh(RenderPrimitive::Triangles);

    auto back_id = pipelines.request(
        1,
        material,
        mesh,
        TestPipelineSpecializer {11, CullMode::Back}
    );
    auto front_id = pipelines.request(
        2,
        material,
        mesh,
        TestPipelineSpecializer {12, CullMode::Front}
    );

    REQUIRE(back_id != front_id);
    REQUIRE(device.render_pipeline_descriptions.empty());

    pipeline_cache.process_queued_pipelines();

    REQUIRE(device.render_pipeline_descriptions.size() == 2);
    CHECK(
        device.render_pipeline_descriptions[0].rasterizer_state.cull_mode ==
        CullMode::Back
    );
    CHECK(
        device.render_pipeline_descriptions[1].rasterizer_state.cull_mode ==
        CullMode::Front
    );
}

TEST_CASE(
    "MeshMaterialPipelines separates mesh pipeline key variants",
    "[pbr][pipelines]"
) {
    FakeGraphicsDevice device;
    MeshViewLayout mesh_view_layout {.layout = create_layout(device)};
    MeshUniforms mesh_uniforms {.resource_layout = create_layout(device)};
    PipelineCache pipeline_cache(device);
    AssetServer asset_server(nullptr);
    Assets<Shader> shaders(nullptr);
    ShaderCache shader_cache(asset_server, shaders, device);
    auto shader_defaults = create_shader_defaults(device);
    auto pipelines = create_mesh_material_pipelines(
        mesh_view_layout,
        mesh_uniforms,
        pipeline_cache,
        shader_cache,
        shader_defaults
    );

    auto material = create_material(device, create_layout(device));
    auto mesh = create_gpu_mesh(RenderPrimitive::Triangles);

    auto depth_id = pipelines.request(
        1,
        material,
        mesh,
        TestPipelineSpecializer {
            11,
            CullMode::Back,
            PbrMeshPipelineKeyFlags::DepthPrepass,
        }
    );
    auto deferred_id = pipelines.request(
        2,
        material,
        mesh,
        TestPipelineSpecializer {
            11,
            CullMode::Back,
            PbrMeshPipelineKeyFlags::DeferredPrepass,
        }
    );

    REQUIRE(depth_id != deferred_id);
    REQUIRE(device.render_pipeline_descriptions.empty());

    pipeline_cache.process_queued_pipelines();

    REQUIRE(device.render_pipeline_descriptions.size() == 2);
}

TEST_CASE(
    "MeshMaterialPipelines find does not request missing pipelines",
    "[pbr][pipelines]"
) {
    FakeGraphicsDevice device;
    MeshViewLayout mesh_view_layout {.layout = create_layout(device)};
    MeshUniforms mesh_uniforms {.resource_layout = create_layout(device)};
    PipelineCache pipeline_cache(device);
    AssetServer asset_server(nullptr);
    Assets<Shader> shaders(nullptr);
    ShaderCache shader_cache(asset_server, shaders, device);
    auto shader_defaults = create_shader_defaults(device);
    auto pipelines = create_mesh_material_pipelines(
        mesh_view_layout,
        mesh_uniforms,
        pipeline_cache,
        shader_cache,
        shader_defaults
    );

    auto material = create_material(device, create_layout(device));
    auto mesh = create_gpu_mesh(RenderPrimitive::Triangles);
    TestPipelineSpecializer specializer {11, CullMode::Back};

    CHECK(pipelines.find(1, material, mesh, specializer) == nullopt);
    pipeline_cache.process_queued_pipelines();

    CHECK(device.render_pipeline_descriptions.empty());
}

TEST_CASE(
    "MeshMaterialPipelines uses PBR defaults for default material shaders",
    "[pbr][pipelines]"
) {
    FakeGraphicsDevice device;
    MeshViewLayout mesh_view_layout {.layout = create_layout(device)};
    MeshUniforms mesh_uniforms {.resource_layout = create_layout(device)};
    PipelineCache pipeline_cache(device);
    AssetServer asset_server(nullptr);
    Assets<Shader> shaders(nullptr);
    ShaderCache shader_cache(asset_server, shaders, device);
    auto shader_defaults = create_shader_defaults(device);
    auto pipelines = create_mesh_material_pipelines(
        mesh_view_layout,
        mesh_uniforms,
        pipeline_cache,
        shader_cache,
        shader_defaults
    );

    auto material =
        create_default_shader_material(device, create_layout(device));
    auto mesh = create_gpu_mesh(RenderPrimitive::Triangles);

    pipelines.request(1, material, mesh, PipelineSpecializer {});
    pipeline_cache.process_queued_pipelines();

    REQUIRE(device.render_pipeline_descriptions.size() == 1);
    REQUIRE(
        device.render_pipeline_descriptions[0].shader_program.shaders.size() ==
        2
    );
    CHECK(
        device.render_pipeline_descriptions[0].shader_program.shaders[0] ==
        shader_defaults.forward_vertex
    );
    CHECK(
        device.render_pipeline_descriptions[0].shader_program.shaders[1] ==
        shader_defaults.forward_fragment
    );
}

TEST_CASE(
    "PBR mesh shader defs follow mesh vertex attributes",
    "[pbr][pipelines]"
) {
    auto mesh = create_gpu_mesh(
        RenderPrimitive::Triangles,
        {
            Mesh::ATTRIBUTE_POSITION,
            Mesh::ATTRIBUTE_NORMAL,
            Mesh::ATTRIBUTE_UV_0,
            Mesh::ATTRIBUTE_TANGENT,
            Mesh::ATTRIBUTE_COLOR,
        }
    );

    auto defs = pbr_mesh_shader_defs(mesh);

    CHECK(has_shader_def(defs, VERTEX_POSITIONS_SHADER_DEF));
    CHECK(has_shader_def(defs, VERTEX_NORMALS_SHADER_DEF));
    CHECK(has_shader_def(defs, VERTEX_UVS_SHADER_DEF));
    CHECK(has_shader_def(defs, VERTEX_TANGENTS_SHADER_DEF));

    auto vertex_layout = pbr_vertex_layout_description(mesh);

    CHECK(has_vertex_attribute(vertex_layout, Mesh::ATTRIBUTE_POSITION.id));
    CHECK(has_vertex_attribute(vertex_layout, Mesh::ATTRIBUTE_NORMAL.id));
    CHECK(has_vertex_attribute(vertex_layout, Mesh::ATTRIBUTE_UV_0.id));
    CHECK(has_vertex_attribute(vertex_layout, Mesh::ATTRIBUTE_TANGENT.id));
    CHECK_FALSE(has_vertex_attribute(vertex_layout, Mesh::ATTRIBUTE_COLOR.id));
}

TEST_CASE(
    "PBR mesh pipeline shader defs follow key flags",
    "[pbr][pipelines]"
) {
    PbrMeshPipelineKey key {
        .flags =
            {
                PbrMeshPipelineKeyFlags::MeshPipeline,
                PbrMeshPipelineKeyFlags::DepthPrepass,
                PbrMeshPipelineKeyFlags::DeferredPrepass,
                PbrMeshPipelineKeyFlags::MayDiscard,
                PbrMeshPipelineKeyFlags::PrepassReadsMaterial,
            },
        .primitive = RenderPrimitive::Triangles,
    };

    auto defs = pbr_mesh_pipeline_shader_defs(key);

    CHECK(has_shader_def(defs, MESH_PIPELINE_SHADER_DEF));
    CHECK(has_shader_def(defs, DEPTH_PREPASS_SHADER_DEF));
    CHECK(has_shader_def(defs, DEFERRED_PREPASS_SHADER_DEF));
    CHECK(has_shader_def(defs, MAY_DISCARD_SHADER_DEF));
    CHECK(has_shader_def(defs, PREPASS_READS_MATERIAL_SHADER_DEF));
    CHECK_FALSE(has_shader_def(defs, SHADOW_PASS_SHADER_DEF));
    CHECK_FALSE(has_shader_def(defs, VXGI_VOXELIZATION_SHADER_DEF));
}

TEST_CASE(
    "PbrMaterialPipelineSpecializer builds descriptor from mesh pipeline key",
    "[pbr][pipelines]"
) {
    FakeGraphicsDevice device;
    MeshViewLayout mesh_view_layout {.layout = create_layout(device)};
    MeshUniforms mesh_uniforms {.resource_layout = create_layout(device)};
    AssetServer asset_server(nullptr);
    Assets<Shader> shaders(nullptr);
    ShaderCache shader_cache(asset_server, shaders, device);
    auto shader_defaults = create_shader_defaults(device);
    device.shader_descriptions.clear();
    PbrMaterialPipelineSpecializer material_pipeline_specializer {
        mesh_view_layout,
        mesh_uniforms,
        shader_cache,
        shader_defaults,
    };

    auto vertex_shader = add_shader(shaders, ShaderStages::Vertex, "test.vert");
    auto fragment_shader =
        add_shader(shaders, ShaderStages::Fragment, "test.frag");
    auto material_layout = create_layout(device);
    auto material = create_shader_request_material(
        device,
        material_layout,
        vertex_shader,
        fragment_shader,
        {ShaderDefVal::bool_def("TEST_VARIANT")}
    );
    auto mesh = create_gpu_mesh(
        RenderPrimitive::Triangles,
        {
            Mesh::ATTRIBUTE_POSITION,
            Mesh::ATTRIBUTE_NORMAL,
            Mesh::ATTRIBUTE_COLOR,
        }
    );
    PbrMeshPipelineKey key {
        .flags =
            {
                PbrMeshPipelineKeyFlags::MeshPipeline,
                PbrMeshPipelineKeyFlags::DeferredPrepass,
                PbrMeshPipelineKeyFlags::MayDiscard,
            },
        .primitive = RenderPrimitive::Lines,
    };
    TestPipelineSpecializer pass_specializer {11, CullMode::Front};

    auto desc = material_pipeline_specializer
                    .specialize(key, material, mesh, pass_specializer);

    CHECK(desc.render_primitive == RenderPrimitive::Lines);
    CHECK(desc.rasterizer_state.cull_mode == CullMode::Front);
    REQUIRE(desc.shader_program.vertex_layouts.size() == 1);
    const auto& vertex_layout = desc.shader_program.vertex_layouts[0];
    CHECK(has_vertex_attribute(vertex_layout, Mesh::ATTRIBUTE_POSITION.id));
    CHECK(has_vertex_attribute(vertex_layout, Mesh::ATTRIBUTE_NORMAL.id));
    CHECK_FALSE(has_vertex_attribute(vertex_layout, Mesh::ATTRIBUTE_COLOR.id));
    REQUIRE(desc.resource_layouts.size() == 3);
    CHECK(desc.resource_layouts[0] == mesh_view_layout.layout);
    CHECK(desc.resource_layouts[1] == mesh_uniforms.resource_layout);
    CHECK(desc.resource_layouts[2] == material_layout);

    REQUIRE(device.shader_descriptions.size() == 2);
    const auto& vertex_shader_desc = device.shader_descriptions[0];
    const auto& fragment_shader_desc = device.shader_descriptions[1];
    CHECK(has_shader_def(vertex_shader_desc.defs, MESH_PIPELINE_SHADER_DEF));
    CHECK(has_shader_def(vertex_shader_desc.defs, DEFERRED_PREPASS_SHADER_DEF));
    CHECK(has_shader_def(vertex_shader_desc.defs, MAY_DISCARD_SHADER_DEF));
    CHECK(has_shader_def(vertex_shader_desc.defs, VERTEX_NORMALS_SHADER_DEF));
    CHECK_FALSE(has_shader_def(vertex_shader_desc.defs, "TEST_VARIANT"));
    CHECK(has_shader_def(fragment_shader_desc.defs, MESH_PIPELINE_SHADER_DEF));
    CHECK(
        has_shader_def(fragment_shader_desc.defs, DEFERRED_PREPASS_SHADER_DEF)
    );
    CHECK(has_shader_def(fragment_shader_desc.defs, MAY_DISCARD_SHADER_DEF));
    CHECK(has_shader_def(fragment_shader_desc.defs, VERTEX_NORMALS_SHADER_DEF));
    CHECK(has_shader_def(fragment_shader_desc.defs, "TEST_VARIANT"));
}

TEST_CASE(
    "PbrMaterialPipelineSpecializer applies material pipeline state",
    "[pbr][pipelines]"
) {
    FakeGraphicsDevice device;
    MeshViewLayout mesh_view_layout {.layout = create_layout(device)};
    MeshUniforms mesh_uniforms {.resource_layout = create_layout(device)};
    AssetServer asset_server(nullptr);
    Assets<Shader> shaders(nullptr);
    ShaderCache shader_cache(asset_server, shaders, device);
    auto shader_defaults = create_shader_defaults(device);
    PbrMaterialPipelineSpecializer material_pipeline_specializer {
        mesh_view_layout,
        mesh_uniforms,
        shader_cache,
        shader_defaults,
    };

    auto material = create_material(
        device,
        create_layout(device),
        1,
        MaterialPipelineState {
            .alpha_mode = MaterialAlphaMode::Blend,
            .cull_mode = CullMode::None,
            .depth_write = false,
        }
    );
    auto mesh = create_gpu_mesh(RenderPrimitive::Triangles);
    PbrMeshPipelineKey key {
        .flags = PbrMeshPipelineKeyFlags::MeshPipeline,
        .primitive = RenderPrimitive::Triangles,
    };

    auto desc = material_pipeline_specializer
                    .specialize(key, material, mesh, PipelineSpecializer {});

    CHECK(desc.rasterizer_state.cull_mode == CullMode::None);
    CHECK_FALSE(desc.depth_stencil_state.depth_write_enabled);
    REQUIRE(desc.blend_state.attachment_states.size() == 1);
    CHECK(desc.blend_state.attachment_states[0].enabled);
    CHECK(
        desc.blend_state.attachment_states[0].source_color_factor ==
        BlendFactor::SrcAlpha
    );
}

TEST_CASE(
    "MeshMaterialPipelines adds alpha mask state to mesh pipeline key defs",
    "[pbr][pipelines]"
) {
    FakeGraphicsDevice device;
    MeshViewLayout mesh_view_layout {.layout = create_layout(device)};
    MeshUniforms mesh_uniforms {.resource_layout = create_layout(device)};
    PipelineCache pipeline_cache(device);
    AssetServer asset_server(nullptr);
    Assets<Shader> shaders(nullptr);
    ShaderCache shader_cache(asset_server, shaders, device);
    auto shader_defaults = create_shader_defaults(device);
    device.shader_descriptions.clear();
    auto pipelines = create_mesh_material_pipelines(
        mesh_view_layout,
        mesh_uniforms,
        pipeline_cache,
        shader_cache,
        shader_defaults
    );

    auto vertex_shader = add_shader(shaders, ShaderStages::Vertex, "test.vert");
    auto fragment_shader =
        add_shader(shaders, ShaderStages::Fragment, "test.frag");
    auto material = create_shader_request_material(
        device,
        create_layout(device),
        vertex_shader,
        fragment_shader,
        {},
        MaterialPipelineState {.alpha_mode = MaterialAlphaMode::Mask}
    );
    auto mesh = create_gpu_mesh(RenderPrimitive::Triangles);

    pipelines.request(1, material, mesh, PipelineSpecializer {});

    REQUIRE(device.shader_descriptions.size() == 2);
    CHECK(has_shader_def(
        device.shader_descriptions[0].defs,
        MAY_DISCARD_SHADER_DEF
    ));
    CHECK(has_shader_def(
        device.shader_descriptions[1].defs,
        MAY_DISCARD_SHADER_DEF
    ));

    pipeline_cache.process_queued_pipelines();

    REQUIRE(device.render_pipeline_descriptions.size() == 1);
    REQUIRE(
        device.render_pipeline_descriptions[0]
            .blend_state.attachment_states.size() == 1
    );
    CHECK_FALSE(device.render_pipeline_descriptions[0]
                    .blend_state.attachment_states[0]
                    .enabled);
}

TEST_CASE(
    "MeshMaterialPipelines resolves material shaders with mesh shader defs",
    "[pbr][pipelines]"
) {
    FakeGraphicsDevice device;
    MeshViewLayout mesh_view_layout {.layout = create_layout(device)};
    MeshUniforms mesh_uniforms {.resource_layout = create_layout(device)};
    PipelineCache pipeline_cache(device);
    AssetServer asset_server(nullptr);
    Assets<Shader> shaders(nullptr);
    ShaderCache shader_cache(asset_server, shaders, device);
    auto shader_defaults = create_shader_defaults(device);
    device.shader_descriptions.clear();
    auto pipelines = create_mesh_material_pipelines(
        mesh_view_layout,
        mesh_uniforms,
        pipeline_cache,
        shader_cache,
        shader_defaults
    );

    auto vertex_shader = add_shader(shaders, ShaderStages::Vertex, "test.vert");
    auto fragment_shader =
        add_shader(shaders, ShaderStages::Fragment, "test.frag");
    auto material = create_shader_request_material(
        device,
        create_layout(device),
        vertex_shader,
        fragment_shader,
        {ShaderDefVal::bool_def("TEST_VARIANT")}
    );
    auto mesh = create_gpu_mesh(
        RenderPrimitive::Triangles,
        {
            Mesh::ATTRIBUTE_POSITION,
            Mesh::ATTRIBUTE_NORMAL,
            Mesh::ATTRIBUTE_UV_0,
            Mesh::ATTRIBUTE_TANGENT,
        }
    );

    pipelines.request(1, material, mesh, PipelineSpecializer {});

    REQUIRE(device.shader_descriptions.size() == 2);
    const auto& vertex_shader_desc = device.shader_descriptions[0];
    const auto& fragment_shader_desc = device.shader_descriptions[1];
    CHECK(has_shader_def(vertex_shader_desc.defs, VERTEX_NORMALS_SHADER_DEF));
    CHECK(has_shader_def(vertex_shader_desc.defs, VERTEX_UVS_SHADER_DEF));
    CHECK(has_shader_def(vertex_shader_desc.defs, VERTEX_TANGENTS_SHADER_DEF));
    CHECK(has_shader_def(fragment_shader_desc.defs, "TEST_VARIANT"));
    CHECK(has_shader_def(fragment_shader_desc.defs, VERTEX_NORMALS_SHADER_DEF));
    CHECK(has_shader_def(fragment_shader_desc.defs, VERTEX_UVS_SHADER_DEF));
    CHECK(
        has_shader_def(fragment_shader_desc.defs, VERTEX_TANGENTS_SHADER_DEF)
    );

    pipeline_cache.process_queued_pipelines();

    REQUIRE(device.render_pipeline_descriptions.size() == 1);
    REQUIRE(
        device.render_pipeline_descriptions[0]
            .shader_program.vertex_layouts.size() == 1
    );
    const auto& vertex_layout =
        device.render_pipeline_descriptions[0].shader_program.vertex_layouts[0];
    CHECK(has_vertex_attribute(vertex_layout, Mesh::ATTRIBUTE_POSITION.id));
    CHECK(has_vertex_attribute(vertex_layout, Mesh::ATTRIBUTE_NORMAL.id));
    CHECK(has_vertex_attribute(vertex_layout, Mesh::ATTRIBUTE_UV_0.id));
    CHECK(has_vertex_attribute(vertex_layout, Mesh::ATTRIBUTE_TANGENT.id));
}

TEST_CASE(
    "MeshMaterialPipelines resolves material shaders with mesh pipeline key "
    "defs",
    "[pbr][pipelines]"
) {
    FakeGraphicsDevice device;
    MeshViewLayout mesh_view_layout {.layout = create_layout(device)};
    MeshUniforms mesh_uniforms {.resource_layout = create_layout(device)};
    PipelineCache pipeline_cache(device);
    AssetServer asset_server(nullptr);
    Assets<Shader> shaders(nullptr);
    ShaderCache shader_cache(asset_server, shaders, device);
    auto shader_defaults = create_shader_defaults(device);
    device.shader_descriptions.clear();
    auto pipelines = create_mesh_material_pipelines(
        mesh_view_layout,
        mesh_uniforms,
        pipeline_cache,
        shader_cache,
        shader_defaults
    );

    auto vertex_shader = add_shader(shaders, ShaderStages::Vertex, "test.vert");
    auto fragment_shader =
        add_shader(shaders, ShaderStages::Fragment, "test.frag");
    auto material = create_shader_request_material(
        device,
        create_layout(device),
        vertex_shader,
        fragment_shader,
        {}
    );
    auto mesh = create_gpu_mesh(RenderPrimitive::Triangles);

    pipelines.request(
        1,
        material,
        mesh,
        TestPipelineSpecializer {
            11,
            CullMode::Back,
            {
                PbrMeshPipelineKeyFlags::DepthPrepass,
                PbrMeshPipelineKeyFlags::DeferredPrepass,
                PbrMeshPipelineKeyFlags::PrepassReadsMaterial,
            },
        }
    );

    REQUIRE(device.shader_descriptions.size() == 2);
    const auto& vertex_shader_desc = device.shader_descriptions[0];
    const auto& fragment_shader_desc = device.shader_descriptions[1];
    CHECK(has_shader_def(vertex_shader_desc.defs, MESH_PIPELINE_SHADER_DEF));
    CHECK(has_shader_def(vertex_shader_desc.defs, DEPTH_PREPASS_SHADER_DEF));
    CHECK(has_shader_def(vertex_shader_desc.defs, DEFERRED_PREPASS_SHADER_DEF));
    CHECK(has_shader_def(
        vertex_shader_desc.defs,
        PREPASS_READS_MATERIAL_SHADER_DEF
    ));
    CHECK(has_shader_def(fragment_shader_desc.defs, MESH_PIPELINE_SHADER_DEF));
    CHECK(has_shader_def(fragment_shader_desc.defs, DEPTH_PREPASS_SHADER_DEF));
    CHECK(
        has_shader_def(fragment_shader_desc.defs, DEFERRED_PREPASS_SHADER_DEF)
    );
    CHECK(has_shader_def(
        fragment_shader_desc.defs,
        PREPASS_READS_MATERIAL_SHADER_DEF
    ));
}

// NOLINTEND(clang-analyzer-cplusplus.NewDeleteLeaks)
