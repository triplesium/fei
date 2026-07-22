#include "pbr/passes/deferred_internal.hpp"
#include "pbr/pipeline_specializer.hpp"

#include <algorithm>
#include <array>
#include <functional>
#include <utility>

namespace fei {

namespace {

class TransparentPipelineSpecializer : public PipelineSpecializer {
    std::shared_ptr<ResourceLayout> m_lighting_layout;

  public:
    explicit TransparentPipelineSpecializer(
        std::shared_ptr<ResourceLayout> lighting_layout
    ) : m_lighting_layout(std::move(lighting_layout)) {}

    std::size_t cache_key() const override {
        return std::hash<const ResourceLayout*> {}(m_lighting_layout.get());
    }

    void specialize(
        RenderPipelineDescription& desc,
        const GpuMesh&,
        const PreparedMaterial&
    ) const override {
        desc.resource_layouts.push_back(m_lighting_layout);
        desc.output_description = OutputDescription {
            .color_attachments =
                {
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
};

void draw_transparent_item(
    CommandBuffer& commands,
    const PipelineCache& pipeline_cache,
    const std::shared_ptr<const ResourceSet>& environment_set,
    const std::shared_ptr<const ResourceSet>& lighting_set,
    const MeshDrawItem& item
) {
    auto pipeline = pipeline_cache.get_render_pipeline(item.pipeline);
    if (!pipeline) {
        return;
    }

    commands.set_render_pipeline(pipeline);
    commands.set_resource_set(0, item.view_set);
    const std::array dynamic_offsets {item.mesh_uniform_dynamic_offset};
    commands.set_resource_set(1, item.mesh_set, dynamic_offsets);
    commands.set_resource_set(2, item.material_set);
    commands.set_resource_set(3, environment_set);
    commands.set_resource_set(4, lighting_set);
    commands.set_vertex_buffer(item.vertex_buffer);

    if (item.index_buffer) {
        commands.set_index_buffer(item.index_buffer, IndexFormat::Uint32);
        commands.draw_indexed(item.index_count);
    } else {
        commands.draw(0, item.vertex_count);
    }
}

} // namespace

void queue_transparent_meshes(
    Query<
        Entity,
        const Mesh3d,
        const MeshMaterial3d<StandardMaterial>,
        const GlobalTransform3d> query_meshes,
    Query<Entity, const MeshViewResourceSet, const GlobalTransform3d>::Filter<
        With<Camera3d>> query_cameras,
    ResRW<TransparentPhase> phase,
    ResRO<RenderAssets<GpuMesh>> gpu_meshes,
    ResRO<MeshUniforms> mesh_uniforms,
    ResRW<MeshMaterialPipelines> mesh_material_pipelines,
    ResRO<RenderAssets<PreparedMaterial>> materials,
    ResRO<ViewVisibleEntities> visible_entities,
    Query<const ShadowMap> query_shadow_maps,
    ResRW<RenderResourceSetCache> resource_sets,
    ResRO<GraphicsDevice> device,
    ResRO<LightingResources> lighting_resources,
    ResRO<RenderingDefaults> rendering_defaults,
    ResRW<PipelineCache>
) {
    phase->clear();
    phase->environment_set.reset();
    phase->lighting_set.reset();
    if (query_cameras.empty()) {
        return;
    }

    auto [camera_entity, camera_resources, camera_transform] =
        query_cameras.first();
    auto visible_meshes =
        visible_entities->get(ViewId::from_source(camera_entity));
    if (!visible_meshes || !camera_resources.environment_resource_set) {
        return;
    }
    phase->environment_set = camera_resources.environment_resource_set;
    std::shared_ptr<Texture> shadow_map;
    for (auto [candidate] : query_shadow_maps) {
        if (candidate.texture) {
            shadow_map = candidate.texture;
            break;
        }
    }
    phase->lighting_set = resource_sets->get_or_create(
        *device,
        "lighting",
        lighting_resources->resource_layout,
        {
            lighting_resources->uniform_buffer,
            shadow_map ? shadow_map : rendering_defaults->default_texture,
            lighting_resources->shadow_map_sampler,
        }
    );
    if (!phase->lighting_set) {
        return;
    }
    const auto camera_position = camera_transform.translation();
    const TransparentPipelineSpecializer specializer {
        lighting_resources->resource_layout
    };

    for (auto [entity, mesh, material_handle, transform] : query_meshes) {
        if (!visible_meshes->contains(entity)) {
            continue;
        }
        auto gpu_mesh = gpu_meshes->get(mesh.mesh.id());
        auto material = materials->get(material_handle.material.id());
        auto mesh_uniform = mesh_uniforms->entries.find(entity);
        if (!gpu_mesh || !material ||
            mesh_uniform == mesh_uniforms->entries.end() ||
            !material_alpha_mode_uses_blend(
                material->pipeline_state().alpha_mode
            )) {
            continue;
        }

        const auto pipeline =
            mesh_material_pipelines
                ->request(entity, *material, *gpu_mesh, specializer);
        const float depth =
            (transform.translation() - camera_position).sqr_magnitude();
        phase->items.push_back(make_mesh_draw_item(
            entity,
            pipeline,
            camera_resources.resource_set,
            mesh_uniforms->resource_set,
            mesh_uniform->second.dynamic_offset,
            material->resource_set(),
            *gpu_mesh,
            depth
        ));
    }

    std::stable_sort(
        phase->items.begin(),
        phase->items.end(),
        [](const MeshDrawItem& lhs, const MeshDrawItem& rhs) {
            return lhs.depth > rhs.depth;
        }
    );
}

void transparent_pass(
    ResRW<RenderFrameContext> frame,
    ResRO<TransparentPhase> phase,
    ResRO<RenderTarget> target,
    ResRO<DeferredViewTargets> targets,
    ResRO<PipelineCache> pipeline_cache
) {
    auto* commands = frame->command_buffer();
    if (!commands || !target->valid() || !targets->valid() ||
        !phase->environment_set || !phase->lighting_set ||
        phase->items.empty()) {
        return;
    }

    commands->begin_render_pass(
        RenderPassDescription {
            .color_attachments =
                {
                    RenderPassColorAttachment {
                        .texture = targets->composite,
                        .load_op = LoadOp::Load,
                    },
                },
            .depth_stencil_attachment = RenderPassDepthStencilAttachment {
                .texture = target->depth_texture,
                .depth_load_op = LoadOp::Load,
                .stencil_load_op = LoadOp::DontCare,
                .depth_store_op = StoreOp::Store,
                .stencil_store_op = StoreOp::DontCare,
            },
        }
    );
    commands->set_viewport(0, 0, targets->width, targets->height);
    for (const auto& item : phase->items) {
        draw_transparent_item(
            *commands,
            *pipeline_cache,
            phase->environment_set,
            phase->lighting_set,
            item
        );
    }
    commands->end_render_pass();
}

} // namespace fei
