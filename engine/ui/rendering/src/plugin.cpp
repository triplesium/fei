#include "ui_rendering/plugin.hpp"

#include "app/app.hpp"
#include "asset/path.hpp"
#include "base/log.hpp"
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
#include "rendering/defaults.hpp"
#include "rendering/gpu_image.hpp"
#include "rendering/pipeline_cache.hpp"
#include "rendering/plugin.hpp"
#include "rendering/render_app.hpp"
#include "rendering/render_asset.hpp"
#include "rendering/render_frame.hpp"
#include "rendering/render_queue.hpp"
#include "rendering/resource_set_cache.hpp"
#include "rendering/shader_cache.hpp"
#include "text/text.hpp"
#include "ui/plugin.hpp"
#include "ui_rendering/renderer.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <utility>
#include <vector>

namespace fei::ui::rendering {

namespace {

struct alignas(16) FrameUniform {
    Vector2 scale;
    Vector2 translate;
};

struct QueuedNode {
    Entity entity;
    ComputedNode computed;
    Color4F color;
    Optional<Rect> clip;
    Optional<ImageNode> image;
    Optional<text::PositionedGlyph> glyph;
    Optional<BorderSide> border_side;
    uint32 stack_index {0};
    uint32 local_order {0};
};

BlendStateDescription ui_blend_state() {
    return BlendStateDescription {{
        BlendAttachmentDescription {
            .enabled = true,
            .color_write_mask = ColorWriteMask::All,
            .source_color_factor = BlendFactor::SrcAlpha,
            .destination_color_factor = BlendFactor::OneMinusSrcAlpha,
            .color_function = BlendFunction::Add,
            .source_alpha_factor = BlendFactor::One,
            .destination_alpha_factor = BlendFactor::OneMinusSrcAlpha,
            .alpha_function = BlendFunction::Add,
        },
    }};
}

void setup_resources(ResRO<GraphicsDevice> device, ResRW<RenderState> state) {
    state->frame_layout = device->create_resource_layout(
        ResourceLayoutDescription::sequencial(
            {ShaderStages::Vertex},
            {uniform_buffer("frame")}
        )
    );
    state->texture_layout = device->create_resource_layout(
        ResourceLayoutDescription::sequencial(
            {ShaderStages::Fragment},
            {texture_read_only("image"), sampler("image_sampler")}
        )
    );
    state->frame_uniform_buffer = device->create_buffer(
        BufferDescription {
            .size = sizeof(FrameUniform),
            .usages = BufferUsages::Uniform,
        }
    );
    state->frame_resource_set = device->create_resource_set(
        ResourceSetDescription {
            .layout = state->frame_layout,
            .resources = {state->frame_uniform_buffer},
            .name = "ui_frame",
        }
    );
    state->default_sampler = device->create_sampler(SamplerDescription::Linear);
}

void prepare_pipeline(
    ResRW<RenderState> state,
    ResRW<ShaderCache> shader_cache,
    ResRW<PipelineCache> pipeline_cache,
    ResRO<MainSwapchain> main_swapchain
) {
    if (!state->frame_layout || !state->texture_layout ||
        !main_swapchain->swapchain) {
        return;
    }
    auto framebuffer = main_swapchain->swapchain->framebuffer();
    if (!framebuffer) {
        return;
    }
    const auto format = main_swapchain->swapchain->color_format();
    if (state->pipeline_id && state->pipeline_format == format) {
        return;
    }

    auto vertex_shader = shader_cache->get_or_compile(
        AssetPath("shader://ui/ui.slang"),
        ShaderStages::Vertex,
        "vertex_main"
    );
    auto fragment_shader = shader_cache->get_or_compile(
        AssetPath("shader://ui/ui.slang"),
        ShaderStages::Fragment,
        "fragment_main"
    );
    state->pipeline_id = pipeline_cache->request_render_pipeline(
        RenderPipelineDescription {
            .blend_state = ui_blend_state(),
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
                                            .offset =
                                                offsetof(Vertex, position),
                                            .format = VertexFormat::Float2,
                                        },
                                        VertexAttributeDescription {
                                            .location = 1,
                                            .offset = offsetof(Vertex, uv),
                                            .format = VertexFormat::Float2,
                                        },
                                        VertexAttributeDescription {
                                            .location = 2,
                                            .offset = offsetof(Vertex, color),
                                            .format = VertexFormat::Float4,
                                        },
                                        VertexAttributeDescription {
                                            .location = 3,
                                            .offset = offsetof(
                                                Vertex,
                                                local_position
                                            ),
                                            .format = VertexFormat::Float2,
                                        },
                                        VertexAttributeDescription {
                                            .location = 4,
                                            .offset = offsetof(Vertex, size),
                                            .format = VertexFormat::Float2,
                                        },
                                        VertexAttributeDescription {
                                            .location = 5,
                                            .offset =
                                                offsetof(Vertex, border_radius),
                                            .format = VertexFormat::Float4,
                                        },
                                        VertexAttributeDescription {
                                            .location = 6,
                                            .offset = offsetof(Vertex, border),
                                            .format = VertexFormat::Float4,
                                        },
                                        VertexAttributeDescription {
                                            .location = 7,
                                            .offset = offsetof(Vertex, kind),
                                            .format = VertexFormat::Float,
                                        },
                                        VertexAttributeDescription {
                                            .location = 8,
                                            .offset =
                                                offsetof(Vertex, border_side),
                                            .format = VertexFormat::Float,
                                        },
                                    },
                                .stride = sizeof(Vertex),
                            },
                        },
                    .shaders = {vertex_shader, fragment_shader},
                },
            .resource_layouts = {state->frame_layout, state->texture_layout},
            .output_description = framebuffer->output_description(),
        }
    );
    state->pipeline_format = format;
}

void queue_nodes(
    Query<
        Entity,
        const ComputedNode,
        const ComputedStackIndex,
        const BackgroundColor> nodes,
    Query<
        Entity,
        const ComputedNode,
        const ComputedStackIndex,
        const BorderColor> borders,
    Query<
        Entity,
        const ComputedNode,
        const ComputedStackIndex,
        const ImageNode,
        const ImageNodeSize> images,
    Query<
        Entity,
        const ComputedNode,
        const ComputedStackIndex,
        const text::TextColor,
        const text::TextLayoutInfo> texts,
    Query<Entity, const CalculatedClip> calculated_clips,
    ResRO<GraphicsDevice> device,
    ResRO<RenderQueue> render_queue,
    ResRO<MainSwapchain> main_swapchain,
    ResRO<RenderState> state,
    ResRO<RenderAssets<GpuImage>> gpu_images,
    ResRO<RenderingDefaults> defaults,
    ResRW<RenderResourceSetCache> resource_sets,
    ResRW<Phase> phase
) {
    phase->clear();
    if (!main_swapchain->swapchain || !state->frame_uniform_buffer) {
        return;
    }
    const auto width = main_swapchain->swapchain->width();
    const auto height = main_swapchain->swapchain->height();
    if (width == 0 || height == 0) {
        return;
    }

    std::vector<QueuedNode> queued;
    std::size_t glyph_count = 0;
    for (const auto& [entity, computed, stack_index, color, layout] : texts) {
        (void)entity;
        (void)computed;
        (void)color;
        if (stack_index.value == ComputedStackIndex::HIDDEN) {
            continue;
        }
        glyph_count += layout.glyphs.size();
    }
    queued.reserve(
        nodes.size() + images.size() + borders.size() * 4 + glyph_count
    );
    const auto clip_for = [&](Entity entity) {
        return calculated_clips.get(entity).transform([](const auto& item) {
            return std::get<1>(item).clip;
        });
    };
    for (const auto& [entity, computed, stack_index, background] : nodes) {
        if (stack_index.value == ComputedStackIndex::HIDDEN ||
            computed.size.x <= 0.0f || computed.size.y <= 0.0f ||
            background.color.a <= 0.0f) {
            continue;
        }
        queued.push_back(
            QueuedNode {
                .entity = entity,
                .computed = computed,
                .color = background.color,
                .clip = clip_for(entity),
                .stack_index = stack_index.value,
            }
        );
    }
    for (const auto& [entity, computed, stack_index, border] : borders) {
        if (stack_index.value == ComputedStackIndex::HIDDEN) {
            continue;
        }
        const std::array sides {
            std::pair {BorderSide::Left, border.left},
            std::pair {BorderSide::Top, border.top},
            std::pair {BorderSide::Right, border.right},
            std::pair {BorderSide::Bottom, border.bottom},
        };
        for (const auto& [side, color] : sides) {
            queued.push_back(
                QueuedNode {
                    .entity = entity,
                    .computed = computed,
                    .color = color,
                    .clip = clip_for(entity),
                    .border_side = side,
                    .stack_index = stack_index.value,
                    .local_order = 1,
                }
            );
        }
    }
    for (const auto& [entity, computed, stack_index, image, image_size] :
         images) {
        (void)image_size;
        if (stack_index.value == ComputedStackIndex::HIDDEN ||
            computed.size.x <= 0.0f || computed.size.y <= 0.0f ||
            image.color.a <= 0.0f || !image.image) {
            continue;
        }
        queued.push_back(
            QueuedNode {
                .entity = entity,
                .computed = computed,
                .color = image.color,
                .clip = clip_for(entity),
                .image = image,
                .stack_index = stack_index.value,
                .local_order = 2,
            }
        );
    }
    for (const auto& [entity, computed, stack_index, color, layout] : texts) {
        if (stack_index.value == ComputedStackIndex::HIDDEN ||
            color.color.a <= 0.0f) {
            continue;
        }
        for (const auto& glyph : layout.glyphs) {
            queued.push_back(
                QueuedNode {
                    .entity = entity,
                    .computed = computed,
                    .color = color.color,
                    .clip = clip_for(entity),
                    .glyph = glyph,
                    .stack_index = stack_index.value,
                    .local_order = 3,
                }
            );
        }
    }
    std::stable_sort(
        queued.begin(),
        queued.end(),
        [](const QueuedNode& lhs, const QueuedNode& rhs) {
            if (lhs.stack_index != rhs.stack_index) {
                return lhs.stack_index < rhs.stack_index;
            }
            if (lhs.local_order != rhs.local_order) {
                return lhs.local_order < rhs.local_order;
            }
            return lhs.entity < rhs.entity;
        }
    );

    if (!defaults->default_texture || !state->default_sampler ||
        !state->texture_layout) {
        return;
    }
    auto default_texture_set = resource_sets->get_or_create(
        *device,
        "ui_default_texture",
        state->texture_layout,
        {defaults->default_texture, state->default_sampler}
    );
    for (const auto& node : queued) {
        if (node.glyph) {
            const auto gpu_image = gpu_images->get(node.glyph->atlas);
            if (!gpu_image || !gpu_image->texture() || !gpu_image->sampler()) {
                continue;
            }
            auto texture_set = resource_sets->get_or_create(
                *device,
                "ui_glyph_atlas",
                state->texture_layout,
                {gpu_image->texture(), gpu_image->sampler()}
            );
            if (const auto quad = make_glyph_quad(
                    node.computed,
                    *node.glyph,
                    node.color,
                    node.clip
                )) {
                phase->append_glyph(*quad, std::move(texture_set));
            }
            continue;
        }
        if (node.border_side) {
            if (const auto quad = make_border_quad(
                    node.computed,
                    node.color,
                    *node.border_side,
                    node.clip
                )) {
                phase->append(*quad, default_texture_set);
            }
            continue;
        }
        if (!node.image) {
            if (const auto quad = make_quad(
                    node.computed,
                    BackgroundColor {.color = node.color},
                    node.clip
                )) {
                phase->append(*quad, default_texture_set);
            }
            continue;
        }
        const auto gpu_image = gpu_images->get(node.image->image);
        if (!gpu_image || !gpu_image->texture() || !gpu_image->sampler()) {
            continue;
        }
        auto texture_set = resource_sets->get_or_create(
            *device,
            "ui_image_texture",
            state->texture_layout,
            {gpu_image->texture(), gpu_image->sampler()}
        );
        if (const auto quad = make_image_quad(
                node.computed,
                *node.image,
                Vector2 {
                    static_cast<float>(gpu_image->texture()->width()),
                    static_cast<float>(gpu_image->texture()->height()),
                },
                node.clip
            )) {
            phase->append(*quad, std::move(texture_set));
        }
    }
    if (phase->vertices.empty()) {
        return;
    }

    const FrameUniform uniform {
        .scale =
            {
                2.0f / static_cast<float>(width),
                -2.0f / static_cast<float>(height),
            },
        .translate = {-1.0f, 1.0f},
    };
    render_queue->write_buffer(state->frame_uniform_buffer, uniform);
    phase->vertices.upload(*device, *render_queue);
    phase->indices.upload(*device, *render_queue);
    phase->active = true;
}

void render_nodes(
    ResRW<PipelineCache> pipeline_cache,
    ResRW<RenderFrameContext> frame_context,
    ResRO<MainSwapchain> main_swapchain,
    ResRO<RenderState> state,
    ResRO<Phase> phase
) {
    if (!phase->active || !main_swapchain->swapchain ||
        !frame_context->recording() || !state->pipeline_id) {
        return;
    }
    auto pipeline = pipeline_cache->get_render_pipeline(*state->pipeline_id);
    if (!pipeline) {
        return;
    }
    auto framebuffer = main_swapchain->swapchain->framebuffer();
    auto* commands = frame_context->command_buffer();
    const auto width = main_swapchain->swapchain->width();
    const auto height = main_swapchain->swapchain->height();
    if (!framebuffer || !commands || width == 0 || height == 0) {
        return;
    }

    commands->begin_render_pass(
        RenderPassDescription {
            .color_attachments =
                {
                    RenderPassColorAttachment {
                        .load_op = LoadOp::Load,
                        .store_op = StoreOp::Store,
                    },
                },
            .framebuffer = std::move(framebuffer),
        }
    );
    commands->set_viewport(0, 0, width, height);
    commands->set_render_pipeline(std::move(pipeline));
    commands->set_vertex_buffer(phase->vertices.buffer());
    commands->set_index_buffer(phase->indices.buffer(), IndexFormat::Uint32);
    commands->set_resource_set(0, state->frame_resource_set);
    for (const auto& batch : phase->batches) {
        if (!batch.texture_set || batch.index_count == 0) {
            continue;
        }
        commands->set_resource_set(1, batch.texture_set);
        commands->draw_indexed(batch.index_count, batch.first_index, 0);
    }
    commands->end_render_pass();
}

void shutdown_renderer(World& world) {
    if (world.has_resource<RenderState>()) {
        world.resource<RenderState>() = RenderState {};
    }
    if (world.has_resource<Phase>()) {
        world.resource<Phase>() = Phase {};
    }
}

} // namespace

void UiRenderingPlugin::dependencies(PluginDependencies& dependencies) const {
    dependencies.require<ui::UiPlugin>().require<RenderingPlugin>();
}

void UiRenderingPlugin::setup(App& app) {
    auto& render_app = app.sub_app<RenderApp>();
    if (!render_app.has_resource<GraphicsDevice>()) {
        fatal(
            "ui::rendering::UiRenderingPlugin requires GraphicsDevice in "
            "Render World"
        );
    }
    if (!render_app.has_resource<MainSwapchain>()) {
        fatal(
            "ui::rendering::UiRenderingPlugin requires MainSwapchain in "
            "Render World"
        );
    }

    add_extract_component<ComputedNode>(app);
    add_extract_component<CalculatedClip>(app);
    add_extract_component<ComputedStackIndex>(app);
    add_extract_component<BackgroundColor>(app);
    add_extract_component<BorderColor>(app);
    add_extract_component<ImageNode>(app);
    add_extract_component<ImageNodeSize>(app);
    add_extract_component<text::TextColor>(app);
    add_extract_component<text::TextLayoutInfo>(app);

    render_app.add_resource(RenderState {})
        .add_resource(Phase {})
        .add_shutdown(shutdown_renderer)
        .add_systems(RenderStartup, setup_resources)
        .add_systems(
            RenderUpdate,
            prepare_pipeline | in_set<RenderingSystems::PrepareResources>() |
                in_set<Systems::Prepare>(),
            queue_nodes | in_set<RenderingSystems::Queue>() |
                in_set<Systems::Queue>(),
            FEI_NAMED_SYSTEM(render_nodes) |
                in_set<RenderingSystems::Overlay>() | in_set<Systems::Render>()
        );
}

} // namespace fei::ui::rendering
