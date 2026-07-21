#include "pbr/mesh_queue.hpp"
#include "pbr/passes/deferred_internal.hpp"
#include "pbr/pipeline_specializer.hpp"

#include <array>
#include <memory>
namespace fei {

namespace {

OutputDescription deferred_gbuffer_output_description() {
    return OutputDescription {
        .color_attachments =
            {
                OutputAttachmentDescription {
                    .format = PixelFormat::Rgba16Float,
                },
                OutputAttachmentDescription {
                    .format = PixelFormat::Rgba16Float,
                },
                OutputAttachmentDescription {
                    .format = PixelFormat::Rgba8Unorm,
                },
                OutputAttachmentDescription {
                    .format = PixelFormat::Rgba8Unorm,
                },
                OutputAttachmentDescription {
                    .format = PixelFormat::Rgba16Float,
                },
            },
        .depth_stencil_attachment =
            OutputAttachmentDescription {
                .format = PixelFormat::Depth32Float,
            },
        .sample_count = TextureSampleCount::Count1,
    };
}

class DeferredPipelineSpecializer : public PipelineSpecializer {
  public:
    explicit DeferredPipelineSpecializer(
        const PbrMeshShaderDefaults& /*shader_defaults*/
    ) {}

    MaterialShaderType vertex_shader_type() const override {
        return MaterialShaderType::PrepassVertex;
    }

    MaterialShaderType fragment_shader_type() const override {
        return MaterialShaderType::PrepassFragment;
    }

    BitFlags<PbrMeshPipelineKeyFlags> mesh_pipeline_flags() const override {
        return {
            PbrMeshPipelineKeyFlags::DepthPrepass,
            PbrMeshPipelineKeyFlags::DeferredPrepass,
            PbrMeshPipelineKeyFlags::PrepassReadsMaterial,
        };
    }

    void specialize(
        RenderPipelineDescription& desc,
        const GpuMesh&,
        const PreparedMaterial&
    ) const override {
        desc.output_description = deferred_gbuffer_output_description();
    }
};

struct DeferredPrepassDrawItem {
    std::shared_ptr<Pipeline> pipeline;
    std::shared_ptr<const ResourceSet> view_set;
    std::shared_ptr<const ResourceSet> mesh_set;
    std::shared_ptr<const ResourceSet> material_set;
    uint32 mesh_uniform_dynamic_offset {};
    std::shared_ptr<const Buffer> vertex_buffer;
    std::shared_ptr<const Buffer> index_buffer;
    uint32 index_count {};
    uint32 vertex_count {};
};

void draw_deferred_prepass_item(
    CommandBuffer& command_buffer,
    const DeferredPrepassDrawItem& item
) {
    if (!item.pipeline) {
        return;
    }

    command_buffer.set_render_pipeline(item.pipeline);
    command_buffer.set_resource_set(0, item.view_set);
    const std::array dynamic_offsets {item.mesh_uniform_dynamic_offset};
    command_buffer.set_resource_set(1, item.mesh_set, dynamic_offsets);
    command_buffer.set_resource_set(2, item.material_set);
    command_buffer.set_vertex_buffer(item.vertex_buffer);

    if (item.index_buffer) {
        command_buffer.set_index_buffer(item.index_buffer, IndexFormat::Uint32);
        command_buffer.draw_indexed(item.index_count);
    } else {
        command_buffer.draw(0, item.vertex_count);
    }
}

} // namespace

void queue_deferred_prepass_meshes(
    Query<
        Entity,
        const Mesh3d,
        const MeshMaterial3d<StandardMaterial>,
        const GlobalTransform3d> query_meshes,
    Query<Entity, const MeshViewResourceSet>::Filter<With<Camera3d>>
        query_cameras,
    ResRW<DeferredPrepassPhase> phase,
    ResRO<RenderAssets<GpuMesh>> gpu_meshes,
    ResRO<MeshUniforms> mesh_uniforms,
    ResRW<MeshMaterialPipelines> mesh_material_pipelines,
    ResRO<RenderAssets<PreparedMaterial>> materials,
    ResRO<ViewVisibleEntities> visible_entities,
    ResRO<PbrMeshShaderDefaults> shader_defaults,
    ResRW<PipelineCache>
) {
    phase->clear();
    if (query_cameras.empty()) {
        return;
    }

    auto [camera_entity, mesh_view_resource_set] = query_cameras.first();
    auto visible_meshes =
        visible_entities->get(ViewId::from_source(camera_entity));
    if (!visible_meshes) {
        return;
    }

    queue_mesh_draw_items(
        query_meshes,
        *phase,
        mesh_view_resource_set.resource_set,
        *gpu_meshes,
        *materials,
        *mesh_uniforms,
        *mesh_material_pipelines,
        DeferredPipelineSpecializer {*shader_defaults},
        [visible_meshes](
            Entity entity,
            const Mesh3d&,
            const MeshMaterial3d<StandardMaterial>&,
            const GlobalTransform3d&
        ) {
            return visible_meshes->contains(entity);
        }
    );
}

void deferred_prepass(
    ResRW<RenderFrameContext> frame,
    ResRO<DeferredPrepassPhase> phase,
    ResRO<RenderTarget> target,
    ResRO<DeferredViewTargets> targets,
    ResRO<PipelineCache> pipeline_cache
) {
    auto* command_buffer = frame->command_buffer();
    if (!command_buffer || !target->valid() || !targets->valid()) {
        return;
    }

    command_buffer->begin_render_pass(
        RenderPassDescription {
            .color_attachments =
                {
                    {.texture = targets->position_ao,
                     .load_op = LoadOp::Clear,
                     .clear_color = Color4F {0.0f, 0.0f, 0.0f, 0.0f}},
                    {.texture = targets->normal_roughness,
                     .load_op = LoadOp::Clear,
                     .clear_color = Color4F {0.0f, 0.0f, 0.0f, 1.0f}},
                    {.texture = targets->albedo_metallic,
                     .load_op = LoadOp::Clear,
                     .clear_color = Color4F {0.0f, 0.0f, 0.0f, 1.0f}},
                    {.texture = targets->specular,
                     .load_op = LoadOp::Clear,
                     .clear_color = Color4F {0.0f, 0.0f, 0.0f, 1.0f}},
                    {.texture = targets->emissive_depth,
                     .load_op = LoadOp::Clear,
                     .clear_color = Color4F {0.0f, 0.0f, 0.0f, 1.0f}},
                },
            .depth_stencil_attachment = RenderPassDepthStencilAttachment {
                .texture = target->depth_texture,
                .depth_load_op = LoadOp::Clear,
                .stencil_load_op = LoadOp::Clear,
                .clear_depth = 1.0f,
                .clear_stencil = 0,
            },
        }
    );
    command_buffer->set_viewport(0, 0, targets->width, targets->height);
    for (const auto& item : phase->items) {
        draw_deferred_prepass_item(
            *command_buffer,
            DeferredPrepassDrawItem {
                .pipeline = pipeline_cache->get_render_pipeline(item.pipeline),
                .view_set = item.view_set,
                .mesh_set = item.mesh_set,
                .material_set = item.material_set,
                .mesh_uniform_dynamic_offset = item.mesh_uniform_dynamic_offset,
                .vertex_buffer = item.vertex_buffer,
                .index_buffer = item.index_buffer,
                .index_count = item.index_count,
                .vertex_count = item.vertex_count,
            }
        );
    }
    command_buffer->end_render_pass();
}

} // namespace fei
