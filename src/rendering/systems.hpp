#pragma once
#include "core/transform.hpp"
#include "ecs/query.hpp"
#include "ecs/system_params.hpp"
#include "graphics/graphics_device.hpp"
#include "rendering/components.hpp"
#include "rendering/material.hpp"
#include "rendering/mesh.hpp"
#include "rendering/render_asset.hpp"
#include "rendering/view.hpp"

namespace fei {

void render_mesh(
    Query<Entity, Mesh3d, MeshMaterial3d, Transform3d> query,
    Res<RenderAssets<GpuMesh>> gpu_meshes,
    Res<RenderAssets<PreparedMaterial>> materials,
    Res<GraphicsDevice> device,
    Res<ViewResource> view_resource,
    Res<MeshUniforms> mesh_uniforms
);

} // namespace fei
