#include "devtools_pbr/plugin.hpp"

#include "app/app.hpp"
#include "asset/assets.hpp"
#include "base/log.hpp"
#include "devtools/bridge.hpp"
#include "devtools/capability.hpp"
#include "devtools/json.hpp"
#include "ecs/commands.hpp"
#include "ecs/query.hpp"
#include "ecs/system_params.hpp"
#include "frame_capture.hpp"
#include "graphics/graphics_device.hpp"
#include "graphics/shader_defs.hpp"
#include "pbr/passes/target.hpp"
#include "pbr/pipeline_specializer.hpp"
#include "pbr/postprocess.hpp"
#include "refl/registry.hpp"
#include "render_targets.hpp"
#include "rendering/mesh/mesh.hpp"
#include "rendering/pipeline_cache.hpp"
#include "rendering/plugin.hpp"
#include "rendering/render_asset.hpp"
#include "rendering/render_frame.hpp"
#include "rendering/shader_cache.hpp"
#include "snapshot_types.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace fei::devtools::pbr {

namespace {

constexpr const char* c_render_targets_schema = "pbr.render_targets.v2";

struct SnapshotPublishState {
    uint64 render_targets_version {0};
};

enum class PreviewPipelineVariant : std::size_t {
    Color,
    ColorMasked,
    ScalarAlphaMasked,
    NormalMasked,
    PositionMasked,
    DepthMasked,
    ToneMappedColor,
    Count,
};

struct RenderTargetPreviewResources {
    std::shared_ptr<ResourceLayout> resource_layout;
    std::shared_ptr<Sampler> sampler;
    std::array<
        CachedRenderPipelineId,
        static_cast<std::size_t>(PreviewPipelineVariant::Count)>
        pipelines {};
    std::shared_ptr<Texture> output;
};

struct SelectedFrameView {
    std::shared_ptr<Texture> texture;
    const RenderTargetDescriptor* target {nullptr};
    const RenderTargetViewDescriptor* view {nullptr};
};

OutputDescription preview_output_description() {
    return OutputDescription {
        .color_attachments =
            {
                OutputAttachmentDescription {
                    .format = PixelFormat::Rgba8Unorm,
                },
            },
        .sample_count = TextureSampleCount::Count1,
    };
}

PreviewPipelineVariant
preview_pipeline_variant(const RenderTargetViewDescriptor& view) {
    switch (view.mode) {
        case PreviewMode::Color:
            return view.geometry_mask ? PreviewPipelineVariant::ColorMasked :
                                        PreviewPipelineVariant::Color;
        case PreviewMode::ScalarAlpha:
            return PreviewPipelineVariant::ScalarAlphaMasked;
        case PreviewMode::Normal:
            return PreviewPipelineVariant::NormalMasked;
        case PreviewMode::Position:
            return PreviewPipelineVariant::PositionMasked;
        case PreviewMode::Depth:
            return PreviewPipelineVariant::DepthMasked;
        case PreviewMode::ToneMappedColor:
            return PreviewPipelineVariant::ToneMappedColor;
    }
    return PreviewPipelineVariant::Color;
}

void setup_render_target_preview_pipeline(
    ResRO<GraphicsDevice> device,
    ResRO<FullscreenQuad> fullscreen_quad,
    ResRO<Assets<Mesh>> meshes,
    ResRW<ShaderCache> shader_cache,
    ResRW<PipelineCache> pipeline_cache,
    ResRW<RenderTargetPreviewResources> resources
) {
    auto mesh = meshes->get(fullscreen_quad->fullscreen_quad_mesh);
    if (!mesh) {
        fatal(
            "Fullscreen quad mesh is not available while setting up the "
            "DevTools PBR preview pipeline"
        );
    }

    resources->resource_layout = device->create_resource_layout(
        ResourceLayoutDescription::sequencial(
            {ShaderStages::Fragment},
            {
                texture_read_only("source"),
                texture_read_only("geometry_mask"),
                sampler("source_sampler"),
            }
        )
    );
    resources->sampler = device->create_sampler(SamplerDescription::Point);

    auto vertex_shader = shader_cache->get_or_compile(
        AssetPath("shader://pbr/quad.slang"),
        ShaderStages::Vertex,
        "vertex_main"
    );
    auto vertex_layout =
        mesh->vertex_buffer_layout().to_vertex_layout_description();
    remove_vertex_input_attribute(vertex_layout, Mesh::ATTRIBUTE_NORMAL.id);
    remove_vertex_input_attribute(vertex_layout, Mesh::ATTRIBUTE_TANGENT.id);

    auto request_pipeline = [&](PreviewMode mode, bool geometry_mask) {
        ShaderDefs defs {
            ShaderDefVal::int_def(
                "DEVTOOLS_PBR_PREVIEW_MODE",
                static_cast<std::int32_t>(mode)
            ),
        };
        if (geometry_mask) {
            defs.push_back(
                ShaderDefVal::bool_def("DEVTOOLS_PBR_GEOMETRY_MASK")
            );
        }
        auto fragment_shader = shader_cache->get_or_compile(
            AssetPath("shader://devtools_pbr/render_target_preview.slang"),
            ShaderStages::Fragment,
            "fragment_main",
            std::move(defs)
        );
        return pipeline_cache->request_render_pipeline(
            RenderPipelineDescription {
                .depth_stencil_state = DepthStencilStateDescription::Disabled,
                .rasterizer_state =
                    RasterizerStateDescription {
                        .cull_mode = CullMode::None,
                    },
                .render_primitive = RenderPrimitive::Triangles,
                .shader_program =
                    ShaderProgramDescription {
                        .vertex_layouts = {vertex_layout},
                        .shaders = {vertex_shader, fragment_shader},
                    },
                .resource_layouts = {resources->resource_layout},
                .output_description = preview_output_description(),
            }
        );
    };
    auto& pipelines = resources->pipelines;
    pipelines[static_cast<std::size_t>(PreviewPipelineVariant::Color)] =
        request_pipeline(PreviewMode::Color, false);
    pipelines[static_cast<std::size_t>(PreviewPipelineVariant::ColorMasked)] =
        request_pipeline(PreviewMode::Color, true);
    pipelines[static_cast<std::size_t>(
        PreviewPipelineVariant::ScalarAlphaMasked
    )] = request_pipeline(PreviewMode::ScalarAlpha, true);
    pipelines[static_cast<std::size_t>(PreviewPipelineVariant::NormalMasked)] =
        request_pipeline(PreviewMode::Normal, true);
    pipelines[static_cast<std::size_t>(
        PreviewPipelineVariant::PositionMasked
    )] = request_pipeline(PreviewMode::Position, true);
    pipelines[static_cast<std::size_t>(PreviewPipelineVariant::DepthMasked)] =
        request_pipeline(PreviewMode::Depth, true);
    pipelines[static_cast<std::size_t>(
        PreviewPipelineVariant::ToneMappedColor
    )] = request_pipeline(PreviewMode::ToneMappedColor, false);
}

bool has_frame_demand(
    const std::string& capability,
    Query<Entity, const Request, const BlobRequest> requests,
    Query<const Subscription> subscriptions
) {
    for (auto [entity, request, blob_request] : requests) {
        (void)entity;
        (void)blob_request;
        if (request.capability == capability) {
            return true;
        }
    }
    for (auto [subscription] : subscriptions) {
        if (subscription.capability == capability) {
            return true;
        }
    }
    return false;
}

SelectedFrameView select_frame_view(
    const DeferredViewTargets& targets,
    FrameCaptureState& state,
    Query<Entity, const Request, const BlobRequest> requests,
    Query<const Subscription> subscriptions
) {
    const auto view_count = render_target_view_count();
    for (std::size_t offset = 0; offset < view_count; ++offset) {
        const auto index = (state.next_view_index + offset) % view_count;
        const auto descriptor = render_target_view_at(index);
        if (!descriptor.target || !descriptor.view ||
            !has_frame_demand(
                descriptor.view->blob_capability,
                requests,
                subscriptions
            )) {
            continue;
        }

        state.next_view_index = (index + 1) % view_count;
        return {
            .texture = resolve_render_target(targets, *descriptor.target),
            .target = descriptor.target,
            .view = descriptor.view,
        };
    }
    return {};
}

void respond_frame_errors(
    Query<Entity, const Request, const BlobRequest> requests,
    Commands commands,
    const std::string& capability,
    std::string message
) {
    for (auto [entity, request, blob_request] : requests) {
        (void)blob_request;
        if (request.capability != capability) {
            continue;
        }
        commands.spawn().add(
            ErrorResponse {
                .token = request.token,
                .capability = request.capability,
                .status = 503,
                .message = message,
            }
        );
        commands.entity(entity).despawn();
    }
}

std::shared_ptr<Texture> ensure_preview_output(
    const GraphicsDevice& device,
    RenderTargetPreviewResources& resources,
    uint32 width,
    uint32 height
) {
    if (resources.output && resources.output->width() == width &&
        resources.output->height() == height) {
        return resources.output;
    }
    resources.output = device.create_texture(
        TextureDescription {
            .width = width,
            .height = height,
            .depth = 1,
            .mip_level = 1,
            .layer = 1,
            .texture_format = PixelFormat::Rgba8Unorm,
            .texture_usage =
                {
                    TextureUsage::RenderTarget,
                    TextureUsage::Sampled,
                },
            .texture_type = TextureType::Texture2D,
        }
    );
    return resources.output;
}

void draw_fullscreen_quad(CommandBuffer& commands, const GpuMesh& mesh) {
    commands.set_vertex_buffer(mesh.vertex_buffer());
    auto index_buffer = mesh.index_buffer();
    if (index_buffer) {
        commands.set_index_buffer(*index_buffer, IndexFormat::Uint32);
        commands.draw_indexed(mesh.index_buffer_size() / sizeof(std::uint32_t));
    } else {
        commands.draw(0, mesh.vertex_count());
    }
}

void render_target_preview(
    ResRO<DeferredViewTargets> targets,
    ResRW<FrameCaptureState> state,
    ResRO<Config> config,
    ResRO<GraphicsDevice> device,
    ResRO<RenderAssets<GpuMesh>> gpu_meshes,
    ResRO<FullscreenQuad> fullscreen_quad,
    ResRO<PipelineCache> pipeline_cache,
    ResRW<RenderFrameContext> frame,
    ResRW<RenderTargetPreviewResources> resources,
    Query<Entity, const Request, const BlobRequest> requests,
    Query<const Subscription> subscriptions,
    Commands commands
) {
    if (state->prepared_capture) {
        return;
    }
    const auto now = std::chrono::steady_clock::now();
    if (!can_capture_now(*config, *state, now) ||
        !state->readback->can_enqueue()) {
        return;
    }

    auto selected =
        select_frame_view(*targets, *state, requests, subscriptions);
    if (!selected.view || !selected.target) {
        return;
    }
    if (!selected.texture) {
        respond_frame_errors(
            requests,
            commands,
            selected.view->blob_capability,
            "The requested PBR render target texture is unavailable"
        );
        return;
    }
    if (!is_previewable(selected.texture)) {
        respond_frame_errors(
            requests,
            commands,
            selected.view->blob_capability,
            "The requested PBR render target cannot be sampled for preview"
        );
        return;
    }

    auto* command_buffer = frame->command_buffer();
    const auto variant = preview_pipeline_variant(*selected.view);
    auto pipeline = pipeline_cache->get_render_pipeline(
        resources->pipelines[static_cast<std::size_t>(variant)]
    );
    auto gpu_mesh = gpu_meshes->get(fullscreen_quad->fullscreen_quad_mesh.id());
    if (!command_buffer || !pipeline || !gpu_mesh ||
        !resources->resource_layout || !resources->sampler ||
        !targets->position_ao) {
        return;
    }

    auto output = ensure_preview_output(
        *device,
        *resources,
        selected.texture->width(),
        selected.texture->height()
    );
    if (!output) {
        respond_frame_errors(
            requests,
            commands,
            selected.view->blob_capability,
            "Failed to allocate the PBR render target preview texture"
        );
        return;
    }

    auto resource_set = device->create_resource_set(
        ResourceSetDescription {
            .layout = resources->resource_layout,
            .resources =
                {
                    selected.texture,
                    targets->position_ao,
                    resources->sampler,
                },
            .name = "devtools.pbr.render_target_preview",
        }
    );
    if (!resource_set) {
        return;
    }

    command_buffer->begin_render_pass(
        RenderPassDescription {
            .color_attachments = {
                RenderPassColorAttachment {
                    .texture = output,
                    .load_op = LoadOp::Clear,
                    .clear_color = Color4F {0.0f, 0.0f, 0.0f, 1.0f},
                },
            },
        }
    );
    command_buffer->set_viewport(0, 0, output->width(), output->height());
    command_buffer->set_render_pipeline(std::move(pipeline));
    command_buffer->set_resource_set(0, std::move(resource_set));
    draw_fullscreen_quad(*command_buffer, *gpu_mesh);
    command_buffer->end_render_pass();

    state->prepared_capture = PreparedFrameCapture {
        .texture = std::move(output),
        .capture = PendingFrameCapture {
            .capability = selected.view->blob_capability,
            .target = selected.target->capture_name,
            .view = selected.view->id,
        },
    };
}

void publish_completed_frame(
    Query<Entity, const Request, const BlobRequest> requests,
    Commands commands,
    std::vector<byte> jpeg,
    std::string capability,
    uint32 width,
    uint32 height,
    std::string target,
    std::string view,
    uint64 version
) {
    std::unordered_map<std::string, std::string> metadata {
        {"width", std::to_string(width)},
        {"height", std::to_string(height)},
        {"target", target},
        {"view", view},
    };

    commands.spawn().add(
        BlobResponse {
            .capability = capability,
            .bytes = jpeg,
            .mime = "image/jpeg",
            .version = version,
            .metadata = metadata,
        }
    );

    for (auto [entity, request, blob_request] : requests) {
        (void)blob_request;
        if (request.capability != capability) {
            continue;
        }
        commands.spawn().add(
            BlobResponse {
                .token = request.token,
                .capability = request.capability,
                .bytes = jpeg,
                .mime = "image/jpeg",
                .version = version,
                .metadata = metadata,
            }
        );
        commands.entity(entity).despawn();
    }
}

void publish_render_targets_snapshot(
    ResRO<DeferredViewTargets> targets,
    ResRW<SnapshotPublishState> state,
    Query<Entity, const Request, const SnapshotRequest> requests,
    Commands commands
) {
    bool requested = false;
    for (auto [entity, request, snapshot_request] : requests) {
        (void)entity;
        (void)snapshot_request;
        if (request.capability == c_render_targets_capability) {
            requested = true;
            break;
        }
    }
    if (!requested) {
        return;
    }

    auto snapshot = make_render_targets_snapshot(*targets);
    auto json = encode_json(Ref(snapshot));
    const auto version = ++state->render_targets_version;
    if (json) {
        commands.spawn().add(
            SnapshotResponse {
                .capability = c_render_targets_capability,
                .json = *json,
                .schema = c_render_targets_schema,
                .version = version,
            }
        );
    } else {
        error(
            "Failed to encode DevTools snapshot {}: {}",
            c_render_targets_capability,
            json.error()
        );
    }

    for (auto [entity, request, snapshot_request] : requests) {
        (void)snapshot_request;
        if (request.capability != c_render_targets_capability) {
            continue;
        }
        if (json) {
            commands.spawn().add(
                SnapshotResponse {
                    .token = request.token,
                    .capability = c_render_targets_capability,
                    .json = *json,
                    .schema = c_render_targets_schema,
                    .version = version,
                }
            );
        } else {
            commands.spawn().add(
                ErrorResponse {
                    .token = request.token,
                    .capability = c_render_targets_capability,
                    .status = 500,
                    .message = json.error(),
                }
            );
        }
        commands.entity(entity).despawn();
    }
}

void publish_and_enqueue_frame_capture(
    ResRW<FrameCaptureState> state,
    ResRO<Config> config,
    Query<Entity, const Request, const BlobRequest> requests,
    Commands commands
) {
    if (auto completed = state->readback->poll()) {
        auto capture = state->take_capture(completed->user_data);
        if (!capture.capability.empty()) {
            auto jpeg = encode_jpeg(
                completed->data,
                completed->width,
                completed->height,
                config->jpeg_quality
            );
            if (jpeg.empty()) {
                respond_frame_errors(
                    requests,
                    commands,
                    capture.capability,
                    "JPEG encoding failed"
                );
            } else {
                const auto version =
                    state->next_frame_version(capture.capability);
                publish_completed_frame(
                    requests,
                    commands,
                    std::move(jpeg),
                    capture.capability,
                    completed->width,
                    completed->height,
                    std::move(capture.target),
                    std::move(capture.view),
                    version
                );
            }
        }
    }

    if (!state->prepared_capture) {
        return;
    }

    auto prepared = std::move(*state->prepared_capture);
    state->prepared_capture.reset();
    auto capability = prepared.capture.capability;
    auto user_data = state->remember_capture(std::move(prepared.capture));
    if (!state->readback->enqueue(
            TextureReadbackRequest {
                .texture = std::move(prepared.texture),
                .output_format = PixelFormat::Rgba8Unorm,
                .user_data = user_data,
            }
        )) {
        state->forget_capture(user_data);
        respond_frame_errors(
            requests,
            commands,
            capability,
            "Texture readback queue is full or unsupported"
        );
        return;
    }

    mark_capture_enqueued(*config, *state, std::chrono::steady_clock::now());
}

void declare_frame_capability(App& app, const char* id, const char* label) {
    declare_capability(
        app.world(),
        id,
        label,
        BlobCapability {
            .mime = "image/jpeg",
            .mode = PublishMode::OnDemand,
            .waitable = true,
        }
    );
}

} // namespace

ProviderPlugin::ProviderPlugin(Config config) : m_config(config) {}

void ProviderPlugin::setup(App& app) {
    if (!app.has_resource<Bridge>()) {
        fatal(
            "devtools::pbr::ProviderPlugin requires devtools::CorePlugin. "
            "Add devtools::CorePlugin before devtools::pbr::ProviderPlugin."
        );
    }
    if (!app.has_resource<DeferredViewTargets>()) {
        fatal(
            "devtools::pbr::ProviderPlugin requires DeferredRenderPlugin. "
            "Add PbrPlugin before devtools::pbr::ProviderPlugin."
        );
    }
    auto& registry = Registry::instance();
    if (!registry.try_get_cls(type_id<RenderTargetViewSnapshot>()) ||
        !registry.try_get_cls(type_id<RenderTargetSnapshot>()) ||
        !registry.try_get_cls(type_id<RenderTargetsSnapshot>())) {
        fatal(
            "devtools::pbr::ProviderPlugin requires ReflectionPlugin. Add "
            "ReflectionPlugin before devtools::pbr::ProviderPlugin."
        );
    }

    declare_capability(
        app.world(),
        c_render_targets_capability,
        "PBR Render Targets",
        SnapshotCapability {
            .schema = c_render_targets_schema,
            .data_type = type_id<RenderTargetsSnapshot>(),
            .mode = PublishMode::Cached,
        }
    );
    for (const auto& target : render_target_descriptors()) {
        for (const auto& view : target.views) {
            declare_frame_capability(app, view.blob_capability, view.label);
        }
    }

    app.add_resource(Config {m_config});
    app.add_resource(SnapshotPublishState {});
    app.add_resource(RenderTargetPreviewResources {});
    app.add_resource(
        FrameCaptureState {
            .readback =
                app.resource<GraphicsDevice>().create_texture_readback(),
        }
    );
    app.add_systems(StartUp, setup_render_target_preview_pipeline);
    app.add_systems(
        RenderUpdate,
        render_target_preview | in_set<RenderingSystems::PostProcess>()
    );
    app.add_systems(RenderEnd, publish_render_targets_snapshot);
    app.add_systems(RenderEnd, publish_and_enqueue_frame_capture);
}

void ProviderPlugin::finish(App&) {}

} // namespace fei::devtools::pbr
