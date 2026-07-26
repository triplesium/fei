#include "sprite/plugin.hpp"

#include "app/app.hpp"
#include "asset/path.hpp"
#include "base/log.hpp"
#include "core/image.hpp"
#include "core/transform.hpp"
#include "ecs/query.hpp"
#include "ecs/system_config.hpp"
#include "ecs/system_params.hpp"
#include "graphics/buffer.hpp"
#include "graphics/command_buffer.hpp"
#include "graphics/enums.hpp"
#include "graphics/graphics_device.hpp"
#include "graphics/pipeline.hpp"
#include "graphics/render_pass.hpp"
#include "graphics/resource.hpp"
#include "graphics/swapchain.hpp"
#include "math/matrix.hpp"
#include "rendering/gpu_image.hpp"
#include "rendering/pipeline_cache.hpp"
#include "rendering/plugin.hpp"
#include "rendering/render_asset.hpp"
#include "rendering/render_frame.hpp"
#include "rendering/render_queue.hpp"
#include "rendering/resource_set_cache.hpp"
#include "rendering/shader_cache.hpp"
#include "sprite/components.hpp"
#include "sprite/renderer.hpp"

#include <algorithm>
#include <cstddef>
#include <utility>
#include <vector>

namespace fei {

namespace {

struct alignas(16) SpriteViewUniform {
    Matrix4x4 clip_from_world;
};

BlendStateDescription sprite_blend_state() {
    return BlendStateDescription {{
        BlendAttachmentDescription {
            .enabled = true,
            .color_write_mask = ColorWriteMask::All,
            .source_color_factor = BlendFactor::One,
            .destination_color_factor = BlendFactor::OneMinusSrcAlpha,
            .color_function = BlendFunction::Add,
            .source_alpha_factor = BlendFactor::One,
            .destination_alpha_factor = BlendFactor::OneMinusSrcAlpha,
            .alpha_function = BlendFunction::Add,
        },
    }};
}

struct QueuedSprite {
    Entity entity;
    Sprite sprite;
    SpriteQuad quad;
};

void setup_sprite_resources(
    ResRO<GraphicsDevice> device,
    ResRW<SpriteRenderState> state
) {
    state->view_layout = device->create_resource_layout(
        ResourceLayoutDescription::sequencial(
            {ShaderStages::Vertex},
            {uniform_buffer("view")}
        )
    );
    state->texture_layout = device->create_resource_layout(
        ResourceLayoutDescription::sequencial(
            {ShaderStages::Fragment},
            {texture_read_only("image"), sampler("image_sampler")}
        )
    );

    state->view_uniform_buffer = device->create_buffer(
        BufferDescription {
            .size = sizeof(SpriteViewUniform),
            .usages = BufferUsages::Uniform,
        }
    );
    state->view_resource_set = device->create_resource_set(
        ResourceSetDescription {
            .layout = state->view_layout,
            .resources = {state->view_uniform_buffer},
            .name = "sprite_view",
        }
    );
}

void prepare_sprite_pipeline(
    ResRW<SpriteRenderState> state,
    ResRW<ShaderCache> shader_cache,
    ResRW<PipelineCache> pipeline_cache,
    ResRO<MainSwapchain> main_swapchain
) {
    if (!main_swapchain->swapchain) {
        return;
    }
    if (!state->view_layout || !state->texture_layout) {
        return;
    }
    const auto framebuffer = main_swapchain->swapchain->framebuffer();
    if (!framebuffer) {
        return;
    }
    const auto format = main_swapchain->swapchain->color_format();
    if (state->pipeline_id && state->pipeline_format == format) {
        return;
    }

    auto vertex_shader = shader_cache->get_or_compile(
        AssetPath("shader://sprite/sprite.slang"),
        ShaderStages::Vertex,
        "vertex_main"
    );
    auto fragment_shader = shader_cache->get_or_compile(
        AssetPath("shader://sprite/sprite.slang"),
        ShaderStages::Fragment,
        "fragment_main"
    );

    state->pipeline_id = pipeline_cache->request_render_pipeline(
        RenderPipelineDescription {
            .blend_state = sprite_blend_state(),
            .depth_stencil_state = DepthStencilStateDescription::Disabled,
            .rasterizer_state =
                RasterizerStateDescription {
                    .cull_mode = CullMode::None,
                },
            .render_primitive = RenderPrimitive::Triangles,
            .shader_program =
                ShaderProgramDescription {
                    .vertex_layouts =
                        {
                            VertexLayoutDescription {
                                .attributes =
                                    {
                                        VertexAttributeDescription {
                                            .location = 0,
                                            .offset = offsetof(
                                                SpriteVertex,
                                                position
                                            ),
                                            .format = VertexFormat::Float2,
                                        },
                                        VertexAttributeDescription {
                                            .location = 1,
                                            .offset =
                                                offsetof(SpriteVertex, uv),
                                            .format = VertexFormat::Float2,
                                        },
                                        VertexAttributeDescription {
                                            .location = 2,
                                            .offset =
                                                offsetof(SpriteVertex, color),
                                            .format = VertexFormat::Float4,
                                        },
                                    },
                                .stride = sizeof(SpriteVertex),
                            },
                        },
                    .shaders = {vertex_shader, fragment_shader},
                },
            .resource_layouts = {state->view_layout, state->texture_layout},
            .output_description = framebuffer->output_description(),
        }
    );
    state->pipeline_format = format;
}

void queue_sprites(
    Query<const Camera2d, const Transform2d> query_cameras,
    Query<Entity, const Sprite, const Transform2d> query_sprites,
    ResRO<GraphicsDevice> device,
    ResRO<RenderQueue> render_queue,
    ResRO<MainSwapchain> main_swapchain,
    ResRO<RenderAssets<GpuImage>> gpu_images,
    ResRW<RenderResourceSetCache> resource_sets,
    ResRO<SpriteRenderState> state,
    ResRW<SpritePhase> phase
) {
    phase->clear();
    if (!main_swapchain->swapchain || query_cameras.empty()) {
        return;
    }
    const auto target_width = main_swapchain->swapchain->width();
    const auto target_height = main_swapchain->swapchain->height();
    if (target_width == 0 || target_height == 0) {
        return;
    }

    auto [camera, camera_transform] = query_cameras.first();
    phase->active = true;
    phase->clear_color = camera.clear_color;
    phase->clip_from_world =
        device->clip_space_transform() * camera_2d_clip_from_world(
                                             camera,
                                             camera_transform,
                                             target_width,
                                             target_height
                                         );
    render_queue->write_buffer(
        state->view_uniform_buffer,
        SpriteViewUniform {.clip_from_world = phase->clip_from_world}
    );

    std::vector<QueuedSprite> queued;
    queued.reserve(query_sprites.size());
    for (const auto& [entity, sprite, transform] : query_sprites) {
        auto quad = make_sprite_quad(sprite, transform);
        if (!is_sprite_quad_visible(quad, phase->clip_from_world)) {
            continue;
        }
        queued.push_back(
            QueuedSprite {
                .entity = entity,
                .sprite = sprite,
                .quad = quad,
            }
        );
    }
    std::stable_sort(
        queued.begin(),
        queued.end(),
        [](const QueuedSprite& lhs, const QueuedSprite& rhs) {
            if (lhs.sprite.layer != rhs.sprite.layer) {
                return lhs.sprite.layer < rhs.sprite.layer;
            }
            return lhs.entity < rhs.entity;
        }
    );

    for (const auto& queued_sprite : queued) {
        const auto gpu_image = gpu_images->get(queued_sprite.sprite.image.id());
        if (!gpu_image || !gpu_image->texture() || !gpu_image->sampler()) {
            continue;
        }
        auto texture_set = resource_sets->get_or_create(
            *device,
            "sprite_texture",
            state->texture_layout,
            {gpu_image->texture(), gpu_image->sampler()}
        );
        if (!texture_set) {
            continue;
        }

        append_sprite(*phase, queued_sprite.quad, std::move(texture_set));
    }
    phase->vertices.upload(*device, *render_queue);
    phase->indices.upload(*device, *render_queue);
}

void render_sprites(
    ResRW<PipelineCache> pipeline_cache,
    ResRW<RenderFrameContext> frame_context,
    ResRO<MainSwapchain> main_swapchain,
    ResRO<SpritePhase> phase,
    ResRO<SpriteRenderState> state
) {
    if (!main_swapchain->swapchain || !phase->active ||
        !frame_context->recording()) {
        return;
    }
    auto framebuffer = main_swapchain->swapchain->framebuffer();
    auto* commands = frame_context->command_buffer();
    if (!framebuffer || !commands || main_swapchain->swapchain->width() == 0 ||
        main_swapchain->swapchain->height() == 0) {
        return;
    }

    commands->begin_render_pass(
        RenderPassDescription {
            .color_attachments =
                {
                    RenderPassColorAttachment {
                        .load_op = LoadOp::Clear,
                        .clear_color = phase->clear_color,
                    },
                },
            .framebuffer = std::move(framebuffer),
        }
    );
    commands->set_viewport(
        0,
        0,
        main_swapchain->swapchain->width(),
        main_swapchain->swapchain->height()
    );

    auto pipeline =
        state->pipeline_id ?
            pipeline_cache->get_render_pipeline(*state->pipeline_id) :
            nullptr;
    if (pipeline && !phase->batches.empty()) {
        commands->set_render_pipeline(std::move(pipeline));
        commands->set_vertex_buffer(phase->vertices.buffer());
        commands->set_index_buffer(
            phase->indices.buffer(),
            IndexFormat::Uint32
        );
        commands->set_resource_set(0, state->view_resource_set);
        for (const auto& batch : phase->batches) {
            if (!batch.texture_set || batch.index_count == 0) {
                continue;
            }
            commands->set_resource_set(1, batch.texture_set);
            commands->draw_indexed(batch.index_count, batch.first_index, 0);
        }
    }
    commands->end_render_pass();
}

} // namespace

void SpritePlugin::setup(App& app) {
    if (!app.has_resource<GraphicsDevice>()) {
        fatal("SpritePlugin requires GraphicsDevice");
    }
    if (!app.has_resource<MainSwapchain>()) {
        fatal("SpritePlugin requires MainSwapchain");
    }
    if (!app.has_plugin<RenderingPlugin>()) {
        fatal("SpritePlugin requires RenderingPlugin to be installed first");
    }
    if (!app.has_plugin<ImagePlugin>()) {
        app.add_plugin<ImagePlugin>();
    }

    app.add_resource(SpriteRenderState {})
        .add_resource(SpritePhase {})
        .add_systems(StartUp, setup_sprite_resources)
        .add_systems(
            RenderUpdate,
            prepare_sprite_pipeline |
                in_set<RenderingSystems::PrepareResources>()
        )
        .add_systems(
            RenderUpdate,
            queue_sprites | in_set<RenderingSystems::Queue>() |
                in_set<SpriteSystems::QueueSprites>()
        )
        .add_systems(
            RenderUpdate,
            FEI_NAMED_SYSTEM(render_sprites) |
                in_set<RenderingSystems::MainPass>() |
                in_set<SpriteSystems::RenderSprites>()
        );
}

} // namespace fei
