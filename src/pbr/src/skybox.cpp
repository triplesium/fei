#include "pbr/skybox.hpp"

#include "asset/assets.hpp"
#include "core/camera.hpp"
#include "ecs/system_config.hpp"
#include "ecs/system_params.hpp"
#include "graphics/render_pass.hpp"
#include "pbr/cubemap.hpp"
#include "pbr/plugin.hpp"
#include "rendering/plugin.hpp"
#include "rendering/render_queue.hpp"
#include "rendering/shader_cache.hpp"
#include "rendering/view.hpp"

#include <memory>
#include <utility>
#include <vector>

namespace fei {

namespace {

OutputDescription render_target_output_description() {
    return OutputDescription {
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

} // namespace

void setup_skybox_resources(
    ResRO<GraphicsDevice> device,
    ResRW<SkyboxResource> skybox_resource,
    ResRW<ShaderCache> shader_cache
) {
    skybox_resource->shader_modules = {
        shader_cache->get_or_compile(
            AssetPath("shader://pbr/skybox.slang"),
            ShaderStages::Vertex,
            {}
        ),
        shader_cache->get_or_compile(
            AssetPath("shader://pbr/skybox.slang"),
            ShaderStages::Fragment,
            {}
        ),
    };

    skybox_resource->view_resource_layout = device->create_resource_layout(
        ResourceLayoutDescription::sequencial(
            ShaderStages::Vertex,
            {uniform_buffer("View")}
        )
    );

    skybox_resource->resource_layout = device->create_resource_layout(
        ResourceLayoutDescription {
            .elements = {
                {
                    .binding = 0,
                    .name = "skybox",
                    .kind = ResourceKind::TextureReadOnly,
                    .stages = ShaderStages::Fragment,
                },
                {
                    .binding = 1,
                    .name = "skybox_sampler",
                    .kind = ResourceKind::Sampler,
                    .stages = ShaderStages::Fragment,
                },
                {
                    .binding = 2,
                    .name = "Skybox",
                    .kind = ResourceKind::UniformBuffer,
                    .stages = {ShaderStages::Vertex, ShaderStages::Fragment},
                }
            },
        }
    );
    skybox_resource->sampler = device->create_sampler(
        SamplerDescription {
            .address_mode_u = SamplerAddressMode::ClampToEdge,
            .address_mode_v = SamplerAddressMode::ClampToEdge,
            .address_mode_w = SamplerAddressMode::ClampToEdge,
        }
    );
    skybox_resource->pipeline = device->create_render_pipeline(
        RenderPipelineDescription {
            .depth_stencil_state =
                DepthStencilStateDescription::DepthOnlyLessEqualRead,
            .rasterizer_state =
                RasterizerStateDescription {
                    .cull_mode = CullMode::None,
                },
            .render_primitive = RenderPrimitive::Triangles,
            .shader_program =
                ShaderProgramDescription {
                    .shaders = skybox_resource->shader_modules,
                },
            .resource_layouts =
                {
                    skybox_resource->view_resource_layout,
                    skybox_resource->resource_layout,
                },
            .output_description = render_target_output_description(),
        }
    );
}

void prepare_skybox_resources(
    Query<
        Entity,
        const Skybox,
        const ViewUniformBuffer,
        SkyboxViewResourceSet>::Filter<With<Camera3d>> existing_views,
    Query<Entity, const Skybox, const ViewUniformBuffer>::
        Filter<With<Camera3d>, Without<SkyboxViewResourceSet>> new_views,
    ResRO<SkyboxResource> skybox_resource,
    ResRW<EquirectToCubemap> equirect_to_cubemap,
    ResRO<GraphicsDevice> device,
    ResRO<Assets<Image>> images,
    ResRO<RenderQueue> render_queue,
    Commands commands
) {
    auto prepare = [&](const Skybox& skybox,
                       const ViewUniformBuffer& view_uniform_buffer,
                       SkyboxViewResourceSet& view_resources) {
        auto cubemap = equirect_to_cubemap->prepare_cubemap(
            *device,
            *images,
            skybox.equirect_map
        );
        if (!cubemap || !skybox_resource->view_resource_layout ||
            !skybox_resource->resource_layout || !skybox_resource->sampler) {
            view_resources.resource_set.reset();
            view_resources.texture = nullptr;
            return false;
        }

        if (!view_resources.uniform_buffer) {
            view_resources.uniform_buffer = device->create_buffer(
                BufferDescription {
                    .size = sizeof(SkyboxUniform),
                    .usages = BufferUsages::Uniform,
                }
            );
        }

        auto uniform = SkyboxUniform {
            .environment_from_world = skybox.rotation.inversed().to_matrix(),
            .brightness = skybox.brightness,
        };
        render_queue->write_buffer(
            view_resources.uniform_buffer,
            0,
            &uniform,
            sizeof(uniform)
        );

        if (!view_resources.view_resource_set ||
            view_resources.view_buffer != view_uniform_buffer.buffer.get()) {
            view_resources.view_resource_set = device->create_resource_set(
                ResourceSetDescription {
                    .layout = skybox_resource->view_resource_layout,
                    .resources = {view_uniform_buffer.buffer},
                    .name = "skybox.view",
                }
            );
            view_resources.view_buffer = view_uniform_buffer.buffer.get();
        }

        if (!view_resources.resource_set ||
            view_resources.texture != cubemap->get()) {
            view_resources.resource_set = device->create_resource_set(
                ResourceSetDescription {
                    .layout = skybox_resource->resource_layout,
                    .resources =
                        {*cubemap,
                         skybox_resource->sampler,
                         view_resources.uniform_buffer},
                    .name = "skybox",
                }
            );
            view_resources.texture = cubemap->get();
        }
        return true;
    };

    for (auto [entity, skybox, view_uniform_buffer, view_resources_component] :
         existing_views) {
        (void)entity;
        prepare(skybox, view_uniform_buffer, view_resources_component.write());
    }

    for (auto [entity, skybox, view_uniform_buffer] : new_views) {
        SkyboxViewResourceSet view_resources;
        if (prepare(skybox, view_uniform_buffer, view_resources)) {
            commands.entity(entity).add(std::move(view_resources));
        }
    }
}

void render_skybox_pass(
    Query<const Skybox, const SkyboxViewResourceSet>::Filter<With<Camera3d>>
        query,
    ResRW<RenderFrameContext> frame,
    ResRO<RenderTarget> target,
    ResRO<DeferredViewTargets> targets,
    ResRO<SkyboxResource> skybox_resource
) {
    auto* command_buffer = frame->command_buffer();
    if (!command_buffer || !target->valid() || !targets->valid() ||
        !skybox_resource->pipeline || query.empty()) {
        return;
    }

    auto [skybox, skybox_view_resources] = query.first();
    (void)skybox;
    if (!skybox_view_resources.resource_set ||
        !skybox_view_resources.view_resource_set) {
        return;
    }

    command_buffer->begin_render_pass(
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
    command_buffer->set_viewport(0, 0, targets->width, targets->height);
    command_buffer->set_render_pipeline(skybox_resource->pipeline);
    command_buffer->set_resource_set(
        0,
        skybox_view_resources.view_resource_set
    );
    command_buffer->set_resource_set(1, skybox_view_resources.resource_set);
    command_buffer->draw(0, 3);
    command_buffer->end_render_pass();
}

void SkyboxPlugin::setup(App& app) {
    app.add_resource(SkyboxResource {})
        .add_systems(
            StartUp,
            setup_skybox_resources | in_set<PbrSystems::StartupSkybox>()
        )
        .add_systems(
            RenderUpdate,
            prepare_skybox_resources |
                in_set<RenderingSystems::PrepareResources>()
        );
}

} // namespace fei
