#include "pbr/mesh_queue.hpp"

#include "../../rendering/tests/test_graphics_device.hpp"
#include "asset/assets.hpp"
#include "pbr/material.hpp"
#include "pbr/mesh_view.hpp"

#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <unordered_map>
#include <vector>

using namespace fei;
using namespace fei::rendering_test;

// NOLINTBEGIN(clang-analyzer-cplusplus.NewDeleteLeaks)

namespace {

struct QueueEntry {
    Entity entity;
    Mesh3d mesh;
    MeshMaterial3d<StandardMaterial> material;
    Transform3d transform;
};

std::shared_ptr<ResourceLayout> create_layout(FakeGraphicsDevice& device) {
    return device.create_resource_layout(ResourceLayoutDescription {});
}

std::shared_ptr<ResourceSet> create_resource_set(FakeGraphicsDevice& device) {
    auto layout = create_layout(device);
    return device.create_resource_set(
        ResourceSetDescription {
            .layout = layout,
            .resources = {},
        }
    );
}

PreparedMaterial create_prepared_material(
    FakeGraphicsDevice& device,
    std::size_t material_hash
) {
    auto layout = create_layout(device);
    auto resource_set = device.create_resource_set(
        ResourceSetDescription {
            .layout = layout,
            .resources = {},
        }
    );
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

    return PreparedMaterial {
        std::move(shaders),
        std::move(layout),
        std::move(resource_set),
        material_hash,
    };
}

GpuMesh create_gpu_mesh() {
    auto vertex_buffer = std::make_shared<FakeBuffer>(
        BufferDescription {.size = 48, .usages = BufferUsages::Vertex}
    );
    MeshVertexBufferLayout vertex_layout {
        .attribute_ids = {Mesh::ATTRIBUTE_POSITION.id},
        .layout = VertexBufferLayout(
            VertexStepMode::Vertex,
            {Mesh::ATTRIBUTE_POSITION.format}
        ),
    };

    return GpuMesh {
        std::move(vertex_buffer),
        nullopt,
        RenderPrimitive::Triangles,
        std::move(vertex_layout),
        0,
        3,
    };
}

void insert_mesh_uniform(
    MeshUniforms& mesh_uniforms,
    FakeGraphicsDevice& device,
    Entity entity
) {
    mesh_uniforms.entries[entity] = MeshUniforms::Entry {
        .entity = entity,
        .uniform_buffer = nullptr,
        .resource_set = create_resource_set(device),
    };
}

} // namespace

TEST_CASE(
    "queue_mesh_draw_items filters entries and skips missing render inputs",
    "[pbr][queue]"
) {
    FakeGraphicsDevice device;
    PipelineCache pipeline_cache(device);
    MeshViewLayout mesh_view_layout {.layout = create_layout(device)};
    MeshUniforms mesh_uniforms {.resource_layout = create_layout(device)};
    MeshMaterialPipelines mesh_material_pipelines(
        mesh_view_layout,
        mesh_uniforms,
        pipeline_cache
    );
    RenderAssets<GpuMesh> gpu_meshes;
    RenderAssets<PreparedMaterial> prepared_materials;
    RenderPhase<MeshDrawItem> phase;

    Assets<Mesh> meshes(nullptr);
    Assets<StandardMaterial> materials(nullptr);
    auto queued_mesh =
        meshes.add(std::make_unique<Mesh>(RenderPrimitive::Triangles));
    auto filtered_mesh =
        meshes.add(std::make_unique<Mesh>(RenderPrimitive::Triangles));
    auto missing_mesh =
        meshes.add(std::make_unique<Mesh>(RenderPrimitive::Triangles));
    auto missing_uniform_mesh =
        meshes.add(std::make_unique<Mesh>(RenderPrimitive::Triangles));
    auto queued_material = materials.add(std::make_unique<StandardMaterial>());
    auto filtered_material =
        materials.add(std::make_unique<StandardMaterial>());

    gpu_meshes.insert(
        queued_mesh.id(),
        std::make_unique<GpuMesh>(create_gpu_mesh())
    );
    gpu_meshes.insert(
        filtered_mesh.id(),
        std::make_unique<GpuMesh>(create_gpu_mesh())
    );
    gpu_meshes.insert(
        missing_uniform_mesh.id(),
        std::make_unique<GpuMesh>(create_gpu_mesh())
    );
    prepared_materials.insert(
        queued_material.id(),
        std::make_unique<PreparedMaterial>(
            create_prepared_material(device, 101)
        )
    );
    prepared_materials.insert(
        filtered_material.id(),
        std::make_unique<PreparedMaterial>(
            create_prepared_material(device, 202)
        )
    );

    insert_mesh_uniform(mesh_uniforms, device, 1);
    insert_mesh_uniform(mesh_uniforms, device, 2);
    insert_mesh_uniform(mesh_uniforms, device, 3);

    std::vector<QueueEntry> query {
        QueueEntry {
            .entity = 1,
            .mesh = Mesh3d {.mesh = queued_mesh},
            .material =
                MeshMaterial3d<StandardMaterial> {
                    .material = queued_material,
                },
        },
        QueueEntry {
            .entity = 2,
            .mesh = Mesh3d {.mesh = filtered_mesh},
            .material =
                MeshMaterial3d<StandardMaterial> {
                    .material = filtered_material,
                },
        },
        QueueEntry {
            .entity = 3,
            .mesh = Mesh3d {.mesh = missing_mesh},
            .material =
                MeshMaterial3d<StandardMaterial> {
                    .material = queued_material,
                },
        },
        QueueEntry {
            .entity = 4,
            .mesh = Mesh3d {.mesh = missing_uniform_mesh},
            .material = MeshMaterial3d<StandardMaterial> {
                .material = queued_material,
            },
        },
    };
    auto view_set = create_resource_set(device);

    queue_mesh_draw_items(
        query,
        phase,
        view_set,
        gpu_meshes,
        prepared_materials,
        mesh_uniforms,
        mesh_material_pipelines,
        PipelineSpecializer {},
        [](Entity entity,
           const Mesh3d&,
           const MeshMaterial3d<StandardMaterial>&,
           const Transform3d&) {
            return entity != 2;
        }
    );

    REQUIRE(phase.items.size() == 1);
    CHECK(phase.items[0].entity == 1);
    CHECK(phase.items[0].view_set == view_set);
    CHECK(phase.items[0].mesh_set == mesh_uniforms.entries.at(1).resource_set);
    CHECK(phase.items[0].vertex_count == 3);
    CHECK(phase.items[0].index_buffer == nullptr);
    CHECK(device.render_pipeline_descriptions.empty());
    CHECK(
        pipeline_cache.get_render_pipeline_state(phase.items[0].pipeline) ==
        CachedPipelineState::Queued
    );

    pipeline_cache.process_queued_pipelines();

    CHECK(
        pipeline_cache.get_render_pipeline_state(phase.items[0].pipeline) ==
        CachedPipelineState::Ready
    );
    CHECK(device.render_pipeline_descriptions.size() == 1);
}

// NOLINTEND(clang-analyzer-cplusplus.NewDeleteLeaks)
