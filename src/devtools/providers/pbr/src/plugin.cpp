#include "devtools_pbr/plugin.hpp"

#include "app/app.hpp"
#include "base/log.hpp"
#include "devtools/bridge.hpp"
#include "devtools/capability.hpp"
#include "devtools/json.hpp"
#include "ecs/commands.hpp"
#include "ecs/query.hpp"
#include "ecs/system_params.hpp"
#include "frame_capture.hpp"
#include "graphics/graphics_device.hpp"
#include "pbr/passes/target.hpp"
#include "refl/registry.hpp"
#include "render_targets.hpp"
#include "snapshot_types.hpp"

#include <chrono>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace fei::devtools::pbr {

namespace {

constexpr const char* c_render_targets_schema = "pbr.render_targets.v1";

struct SnapshotPublishState {
    uint64 render_targets_version {0};
};

struct SelectedFrameTarget {
    std::shared_ptr<Texture> texture;
    std::string capability;
    std::string name;
};

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

SelectedFrameTarget select_frame_target(
    const DeferredViewTargets& targets,
    FrameCaptureState& state,
    Query<Entity, const Request, const BlobRequest> requests,
    Query<const Subscription> subscriptions
) {
    const auto& descriptors = render_target_descriptors();
    for (std::size_t offset = 0; offset < descriptors.size(); ++offset) {
        const auto index =
            (state.next_target_index + offset) % descriptors.size();
        const auto& descriptor = descriptors[index];
        if (descriptor.blob_capability[0] == '\0' ||
            !has_frame_demand(
                descriptor.blob_capability,
                requests,
                subscriptions
            )) {
            continue;
        }

        state.next_target_index = (index + 1) % descriptors.size();
        return {
            .texture = resolve_render_target(targets, descriptor),
            .capability = descriptor.blob_capability,
            .name = descriptor.capture_name,
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

void publish_completed_frame(
    Query<Entity, const Request, const BlobRequest> requests,
    Commands commands,
    std::vector<byte> jpeg,
    std::string capability,
    uint32 width,
    uint32 height,
    std::string target,
    uint64 version
) {
    std::unordered_map<std::string, std::string> metadata {
        {"width", std::to_string(width)},
        {"height", std::to_string(height)},
        {"target", target},
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

void capture_rendering_frame(
    ResRO<DeferredViewTargets> targets,
    ResRW<FrameCaptureState> state,
    ResRO<Config> config,
    Query<Entity, const Request, const BlobRequest> requests,
    Query<const Subscription> subscriptions,
    Commands commands
) {
    if (auto completed = state->readback->poll()) {
        auto capture = state->take_capture(completed->user_data);
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
            publish_completed_frame(
                requests,
                commands,
                std::move(jpeg),
                capture.capability,
                completed->width,
                completed->height,
                std::move(capture.target),
                state->next_frame_version(capture.capability)
            );
        }
    }

    auto now = std::chrono::steady_clock::now();
    if (!can_capture_now(*config, *state, now) ||
        !state->readback->can_enqueue()) {
        return;
    }

    auto target =
        select_frame_target(*targets, *state, requests, subscriptions);
    if (target.capability.empty()) {
        return;
    }
    if (!target.texture) {
        respond_frame_errors(
            requests,
            commands,
            target.capability,
            "The requested PBR render target texture is unavailable"
        );
        return;
    }
    if (!is_directly_capturable(target.texture)) {
        respond_frame_errors(
            requests,
            commands,
            target.capability,
            "DevTools frame capture requires a 2D Rgba8Unorm texture"
        );
        return;
    }

    auto user_data =
        state->remember_capture(target.capability, std::move(target.name));
    if (!state->readback->enqueue(
            TextureReadbackRequest {
                .texture = std::move(target.texture),
                .output_format = PixelFormat::Rgba8Unorm,
                .user_data = user_data,
            }
        )) {
        state->forget_capture(user_data);
        respond_frame_errors(
            requests,
            commands,
            target.capability,
            "Texture readback queue is full or unsupported"
        );
        return;
    }

    mark_capture_enqueued(*config, *state, now);
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
    if (!registry.try_get_cls(type_id<RenderTargetSnapshot>()) ||
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
    declare_frame_capability(app, c_composite_capability, "Rendered Frame");
    declare_frame_capability(
        app,
        c_albedo_metallic_capability,
        "Albedo / Metallic"
    );
    declare_frame_capability(app, c_specular_capability, "Specular");

    app.add_resource(Config {m_config});
    app.add_resource(SnapshotPublishState {});
    app.add_resource(
        FrameCaptureState {
            .readback =
                app.resource<GraphicsDevice>().create_texture_readback(),
        }
    );
    app.add_systems(RenderEnd, publish_render_targets_snapshot);
    app.add_systems(RenderEnd, capture_rendering_frame);
}

void ProviderPlugin::finish(App&) {}

} // namespace fei::devtools::pbr
