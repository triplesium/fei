#include "pbr/pipelines.hpp"

#include "base/optional.hpp"
#include "ecs/fwd.hpp"
#include "graphics/enums.hpp"
#include "graphics/pipeline.hpp"
#include "graphics/resource.hpp"
#include "graphics/shader_module.hpp"
#include "pbr/mesh_view.hpp"
#include "pbr/pipeline_specializer.hpp"
#include "rendering/material.hpp"
#include "rendering/mesh/mesh.hpp"
#include "rendering/tests/test_graphics_device.hpp"

#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <unordered_map>

using namespace fei;
using namespace fei::rendering_test;

namespace {

class TestPipelineSpecializer : public PipelineSpecializer {
  private:
    std::size_t m_cache_key {0};
    CullMode m_cull_mode {CullMode::Back};

  public:
    TestPipelineSpecializer(std::size_t cache_key, CullMode cull_mode) :
        m_cache_key(cache_key), m_cull_mode(cull_mode) {}

    std::size_t cache_key() const override { return m_cache_key; }

    void specialize(
        RenderPipelineDescription& desc,
        const GpuMesh&,
        const PreparedMaterial&
    ) const override {
        desc.rasterizer_state.cull_mode = m_cull_mode;
    }
};

std::shared_ptr<ResourceLayout> create_layout(FakeGraphicsDevice& device) {
    return device.create_resource_layout(ResourceLayoutDescription {});
}

PreparedMaterial create_material(
    FakeGraphicsDevice& device,
    std::shared_ptr<ResourceLayout> layout,
    std::size_t material_hash = 1
) {
    std::unordered_map<MaterialShaderType, std::shared_ptr<ShaderModule>>
        shaders {
            {MaterialShaderType::Vertex,
             device.create_shader_module(
                 ShaderDescription {.stage = ShaderStages::Vertex}
             )},
            {MaterialShaderType::Fragment,
             device.create_shader_module(
                 ShaderDescription {.stage = ShaderStages::Fragment}
             )},
        };

    auto resource_set = device.create_resource_set(
        ResourceSetDescription {
            .layout = layout,
            .resources = {},
        }
    );
    return PreparedMaterial {
        std::move(shaders),
        std::move(layout),
        std::move(resource_set),
        material_hash
    };
}

GpuMesh create_gpu_mesh(RenderPrimitive primitive) {
    MeshVertexBufferLayout vertex_layout {
        .attribute_ids = {Mesh::ATTRIBUTE_POSITION.id},
        .layout = VertexBufferLayout(
            VertexStepMode::Vertex,
            {Mesh::ATTRIBUTE_POSITION.format}
        )
    };

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
    PipelineCache& pipeline_cache
) {
    return MeshMaterialPipelines {
        mesh_view_layout,
        mesh_uniforms,
        pipeline_cache,
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
    auto pipelines = create_mesh_material_pipelines(
        mesh_view_layout,
        mesh_uniforms,
        pipeline_cache
    );

    auto material = create_material(device, create_layout(device));
    auto mesh = create_gpu_mesh(RenderPrimitive::Triangles);
    TestPipelineSpecializer specializer {11, CullMode::Back};

    auto first_id = pipelines.get(1, material, mesh, specializer);
    auto second_id = pipelines.get(2, material, mesh, specializer);

    REQUIRE(first_id == second_id);
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
    auto pipelines = create_mesh_material_pipelines(
        mesh_view_layout,
        mesh_uniforms,
        pipeline_cache
    );

    auto material = create_material(device, create_layout(device));
    TestPipelineSpecializer specializer {11, CullMode::Back};

    auto triangle_id = pipelines.get(
        1,
        material,
        create_gpu_mesh(RenderPrimitive::Triangles),
        specializer
    );
    auto line_id = pipelines.get(
        2,
        material,
        create_gpu_mesh(RenderPrimitive::Lines),
        specializer
    );

    REQUIRE(triangle_id != line_id);
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
    auto pipelines = create_mesh_material_pipelines(
        mesh_view_layout,
        mesh_uniforms,
        pipeline_cache
    );

    auto material = create_material(device, create_layout(device));
    auto mesh = create_gpu_mesh(RenderPrimitive::Triangles);

    auto back_id = pipelines.get(
        1,
        material,
        mesh,
        TestPipelineSpecializer {11, CullMode::Back}
    );
    auto front_id = pipelines.get(
        2,
        material,
        mesh,
        TestPipelineSpecializer {12, CullMode::Front}
    );

    REQUIRE(back_id != front_id);
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
