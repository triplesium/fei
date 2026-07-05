#include "pbr/skybox.hpp"

#include "asset/assets.hpp"
#include "asset/server.hpp"
#include "ecs/system_config.hpp"
#include "ecs/system_params.hpp"
#include "pbr/cubemap.hpp"
#include "pbr/mesh_view.hpp"
#include "rendering/mesh/mesh.hpp"
#include "rendering/plugin.hpp"
#include "rendering/shader.hpp"

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
                    .format = PixelFormat::Rgba8Unorm,
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
    ResRW<Assets<Mesh>> meshes,
    ResRW<Assets<Shader>> shaders,
    ResRW<SkyboxResource> skybox_resource,
    ResRW<AssetServer> asset_server,
    ResRO<MeshViewLayout> mesh_view_layout
) {
    auto mesh = std::make_unique<Mesh>(RenderPrimitive::Triangles);
    std::vector<std::array<float, 3>> positions {
        {-1.0f, 1.0f, -1.0f},  {-1.0f, -1.0f, -1.0f}, {1.0f, -1.0f, -1.0f},
        {1.0f, -1.0f, -1.0f},  {1.0f, 1.0f, -1.0f},   {-1.0f, 1.0f, -1.0f},
        {-1.0f, -1.0f, 1.0f},  {-1.0f, -1.0f, -1.0f}, {-1.0f, 1.0f, -1.0f},
        {-1.0f, 1.0f, -1.0f},  {-1.0f, 1.0f, 1.0f},   {-1.0f, -1.0f, 1.0f},
        {1.0f, -1.0f, -1.0f},  {1.0f, -1.0f, 1.0f},   {1.0f, 1.0f, 1.0f},
        {1.0f, 1.0f, 1.0f},    {1.0f, 1.0f, -1.0f},   {1.0f, -1.0f, -1.0f},
        {-1.0f, -1.0f, 1.0f},  {-1.0f, 1.0f, 1.0f},   {1.0f, 1.0f, 1.0f},
        {1.0f, 1.0f, 1.0f},    {1.0f, -1.0f, 1.0f},   {-1.0f, -1.0f, 1.0f},
        {-1.0f, 1.0f, -1.0f},  {1.0f, 1.0f, -1.0f},   {1.0f, 1.0f, 1.0f},
        {1.0f, 1.0f, 1.0f},    {-1.0f, 1.0f, 1.0f},   {-1.0f, 1.0f, -1.0f},
        {-1.0f, -1.0f, -1.0f}, {-1.0f, -1.0f, 1.0f},  {1.0f, -1.0f, -1.0f},
        {1.0f, -1.0f, -1.0f},  {-1.0f, -1.0f, 1.0f},  {1.0f, -1.0f, 1.0f}
    };
    mesh->insert_attribute(Mesh::ATTRIBUTE_POSITION, positions);
    auto vertex_layout =
        mesh->vertex_buffer_layout().to_vertex_layout_description();
    skybox_resource->mesh = meshes->add(std::move(mesh));
    auto vertex_shader_handle =
        asset_server->load<Shader>("shader://skybox.vert");
    auto fragment_shader_handle =
        asset_server->load<Shader>("shader://skybox.frag");
    skybox_resource->shader_modules = {
        device->create_shader_module(
            shaders->get(vertex_shader_handle)->description()
        ),
        device->create_shader_module(
            shaders->get(fragment_shader_handle)->description()
        )
    };

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
                DepthStencilStateDescription::DepthOnlyLessEqual,
            .rasterizer_state = {},
            .render_primitive = RenderPrimitive::Triangles,
            .shader_program =
                ShaderProgramDescription {
                    .vertex_layouts = {std::move(vertex_layout)},
                    .shaders = skybox_resource->shader_modules,
                },
            .resource_layouts =
                {
                    mesh_view_layout->layout,
                    skybox_resource->resource_layout,
                },
            .output_description = render_target_output_description(),
        }
    );
}

void prepare_skybox_resources(
    Query<const Skybox> query,
    ResRW<SkyboxResource> skybox_resource,
    ResRW<EquirectToCubemap> equirect_to_cubemap,
    ResRO<GraphicsDevice> device,
    ResRO<Assets<Image>> images
) {
    for (auto [skybox] : query) {
        auto cubemap = equirect_to_cubemap->prepare_cubemap(
            *device,
            *images,
            skybox.equirect_map
        );
        if (!cubemap || !skybox_resource->resource_layout ||
            !skybox_resource->sampler) {
            continue;
        }
        if (skybox_resource->resource_set &&
            skybox_resource->resource_set_image == skybox.equirect_map.id()) {
            continue;
        }

        skybox_resource->resource_set = device->create_resource_set(
            ResourceSetDescription {
                .layout = skybox_resource->resource_layout,
                .resources = {*cubemap, skybox_resource->sampler},
                .name = "skybox",
            }
        );
        skybox_resource->resource_set_image = skybox.equirect_map.id();
    }
}

void SkyboxPlugin::setup(App& app) {
    app.add_resource(SkyboxResource {})
        .add_systems(
            StartUp,
            setup_skybox_resources | after(init_mesh_view_layout)
        )
        .add_systems(
            RenderUpdate,
            prepare_skybox_resources |
                in_set<RenderingSystems::PrepareResources>()
        );
}

} // namespace fei
