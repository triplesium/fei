#pragma once
#include "pbr/pipelines.hpp"
#include "rendering/components.hpp" // NOLINT(misc-include-cleaner)
#include "rendering/mesh/mesh_uniform.hpp"
#include "rendering/render_asset.hpp"
#include "rendering/render_phase.hpp"

#include <functional>
#include <memory>
#include <utility>

namespace fei {

template<class QueryT, class PhaseT, class SpecializerT, class ShouldQueueT>
void queue_mesh_draw_items(
    QueryT&& query,
    PhaseT& phase,
    std::shared_ptr<const ResourceSet> view_resource_set,
    const RenderAssets<GpuMesh>& gpu_meshes,
    const RenderAssets<PreparedMaterial>& materials,
    const MeshUniforms& mesh_uniforms,
    MeshMaterialPipelines& mesh_material_pipelines,
    const SpecializerT& specializer,
    ShouldQueueT&& should_queue
) {
    for (auto&& [entity, mesh3d, material3d, transform3d] : query) {
        if (!std::invoke(
                should_queue,
                entity,
                mesh3d,
                material3d,
                transform3d
            )) {
            continue;
        }

        auto gpu_mesh_opt = gpu_meshes.get(mesh3d.mesh.id());
        auto material_opt = materials.get(material3d.material.id());
        if (!gpu_mesh_opt || !material_opt) {
            continue;
        }

        auto& gpu_mesh = *gpu_mesh_opt;
        auto& material = *material_opt;
        auto mesh_uniform_it = mesh_uniforms.entries.find(entity);
        if (mesh_uniform_it == mesh_uniforms.entries.end()) {
            continue;
        }

        auto pipeline_id =
            mesh_material_pipelines
                .request(entity, material, gpu_mesh, specializer);
        phase.items.push_back(make_mesh_draw_item(
            entity,
            pipeline_id,
            view_resource_set,
            mesh_uniforms.resource_set,
            mesh_uniform_it->second.dynamic_offset,
            material.resource_set(),
            gpu_mesh
        ));
    }

    sort_by_pipeline(phase);
}

template<class QueryT, class PhaseT, class SpecializerT>
void queue_mesh_draw_items(
    QueryT&& query,
    PhaseT& phase,
    std::shared_ptr<const ResourceSet> view_resource_set,
    const RenderAssets<GpuMesh>& gpu_meshes,
    const RenderAssets<PreparedMaterial>& materials,
    const MeshUniforms& mesh_uniforms,
    MeshMaterialPipelines& mesh_material_pipelines,
    const SpecializerT& specializer
) {
    queue_mesh_draw_items(
        std::forward<QueryT>(query),
        phase,
        std::move(view_resource_set),
        gpu_meshes,
        materials,
        mesh_uniforms,
        mesh_material_pipelines,
        specializer,
        [](Entity, const auto&, const auto&, const auto&) {
            return true;
        }
    );
}

} // namespace fei
