#include "devtools_rendering/plugin.hpp"

#include "app/app.hpp"
#include "base/log.hpp"
#include "devtools/bridge.hpp"
#include "devtools/capability.hpp"
#include "devtools/json.hpp"
#include "ecs/commands.hpp"
#include "ecs/query.hpp"
#include "ecs/system_params.hpp"
#include "ecs/world.hpp"
#include "graphics/graphics_device.hpp"
#include "graphics/texture.hpp"
#include "graphics/texture_readback.hpp"
#include "pbr/passes/target.hpp"
#include "refl/registry.hpp"
#include "snapshot_demand.hpp"
#include "snapshot_types.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb/stb_image_write.h>

namespace fei::devtools::rendering {

namespace {

constexpr const char* c_render_schedule_schema = "rendering.render_schedule.v1";
constexpr const char* c_graphics_cache_schema = "graphics.cache.v2";

struct SnapshotPublishState {
    uint64 render_schedule_version {0};
    uint64 graphics_cache_version {0};
};

struct SelectedFrameTarget {
    std::shared_ptr<Texture> texture;
    std::string name;
};

struct FrameCaptureState {
    std::shared_ptr<TextureReadback> readback;
    std::unordered_map<uint64, std::string> target_names;
    std::chrono::steady_clock::time_point next_capture_at;
    uint64 next_user_data {1};
    uint64 frame_version {0};

    uint64 remember_target(std::string name) {
        auto user_data = next_user_data++;
        target_names[user_data] = std::move(name);
        return user_data;
    }

    std::string take_target(uint64 user_data) {
        auto iter = target_names.find(user_data);
        if (iter == target_names.end()) {
            return {};
        }
        auto name = std::move(iter->second);
        target_names.erase(iter);
        return name;
    }

    void forget_target(uint64 user_data) { target_names.erase(user_data); }
};

bool can_capture_now(
    const Config& config,
    const FrameCaptureState& state,
    std::chrono::steady_clock::time_point now
) {
    return config.max_capture_fps == 0 || state.next_capture_at <= now;
}

void mark_capture_enqueued(
    const Config& config,
    FrameCaptureState& state,
    std::chrono::steady_clock::time_point now
) {
    if (config.max_capture_fps == 0) {
        return;
    }

    auto interval = std::chrono::nanoseconds {
        1'000'000'000ULL /
        static_cast<unsigned long long>(config.max_capture_fps)
    };
    state.next_capture_at = now + interval;
}

SelectedFrameTarget
select_frame_target(const Optional<ResRO<DeferredViewTargets>>& targets) {
    if (targets && (*targets)->composite) {
        return {
            .texture = (*targets)->composite,
            .name = "deferred_view_targets.composite",
        };
    }
    return {};
}

void append_jpeg_bytes(void* context, void* data, int size) {
    auto& bytes = *static_cast<std::vector<byte>*>(context);
    auto* first = static_cast<byte*>(data);
    bytes.insert(bytes.end(), first, first + size);
}

std::vector<unsigned char> rgba_to_flipped_rgb(
    const std::vector<byte>& rgba,
    uint32 width,
    uint32 height
) {
    std::vector<unsigned char> rgb(
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 3
    );
    auto row_rgba_size = static_cast<std::size_t>(width) * 4;
    auto row_rgb_size = static_cast<std::size_t>(width) * 3;

    for (uint32 dst_y = 0; dst_y < height; ++dst_y) {
        auto src_y = height - dst_y - 1;
        auto* src = reinterpret_cast<const unsigned char*>(rgba.data()) +
                    static_cast<std::size_t>(src_y) * row_rgba_size;
        auto* dst = rgb.data() + static_cast<std::size_t>(dst_y) * row_rgb_size;
        for (uint32 x = 0; x < width; ++x) {
            dst[x * 3 + 0] = src[x * 4 + 0];
            dst[x * 3 + 1] = src[x * 4 + 1];
            dst[x * 3 + 2] = src[x * 4 + 2];
        }
    }
    return rgb;
}

std::vector<byte> encode_jpeg(
    const std::vector<byte>& rgba,
    uint32 width,
    uint32 height,
    int quality
) {
    if (rgba.empty()) {
        return {};
    }

    auto rgb = rgba_to_flipped_rgb(rgba, width, height);
    std::vector<byte> jpeg;
    auto ok = stbi_write_jpg_to_func(
        append_jpeg_bytes,
        &jpeg,
        static_cast<int>(width),
        static_cast<int>(height),
        3,
        rgb.data(),
        std::clamp(quality, 1, 100)
    );
    if (ok == 0) {
        return {};
    }
    return jpeg;
}

void respond_snapshot_requests(
    Query<Entity, const Request, const SnapshotRequest> requests,
    Commands commands,
    const std::string& capability,
    const std::string& schema,
    const std::string& json,
    uint64 version
) {
    auto& world = commands.world();
    for (auto [entity, request, snapshot_request] : requests) {
        (void)snapshot_request;
        if (request.capability != capability) {
            continue;
        }
        if (!world.has_entity(entity)) {
            continue;
        }
        commands.spawn().add(
            SnapshotResponse {
                .token = request.token,
                .capability = capability,
                .json = json,
                .schema = schema,
                .version = version,
            }
        );
        commands.entity(entity).despawn();
    }
}

void publish_snapshot_result(
    Query<Entity, const Request, const SnapshotRequest> requests,
    Commands commands,
    const std::string& capability,
    const std::string& schema,
    Result<std::string, std::string> json,
    uint64 version
) {
    if (!json) {
        error(
            "Failed to encode DevTools snapshot {}: {}",
            capability,
            json.error()
        );
        for (auto [entity, request, snapshot_request] : requests) {
            (void)snapshot_request;
            if (request.capability != capability) {
                continue;
            }
            commands.spawn().add(
                ErrorResponse {
                    .token = request.token,
                    .capability = request.capability,
                    .status = 500,
                    .message = json.error(),
                }
            );
            commands.entity(entity).despawn();
        }
        return;
    }

    commands.spawn().add(
        SnapshotResponse {
            .capability = capability,
            .json = *json,
            .schema = schema,
            .version = version,
        }
    );
    respond_snapshot_requests(
        requests,
        commands,
        capability,
        schema,
        *json,
        version
    );
}

void publish_rendering_snapshots(
    ResRO<GraphicsDevice> graphics_device,
    ResRW<SnapshotPublishState> state,
    Query<Entity, const Request, const SnapshotRequest> requests,
    Commands commands
) {
    SnapshotDemand demand;
    for (auto [entity, request, snapshot_request] : requests) {
        (void)entity;
        (void)snapshot_request;
        demand.include(request.capability);
    }
    if (!demand.any()) {
        return;
    }

    if (demand.render_schedule) {
        RenderScheduleSnapshot snapshot;
        if (auto debug = commands.world().schedule_debug_info(RenderUpdate)) {
            snapshot = make_render_schedule_snapshot(*debug);
        }
        publish_snapshot_result(
            requests,
            commands,
            c_render_schedule_capability,
            c_render_schedule_schema,
            encode_json(Ref(snapshot)),
            ++state->render_schedule_version
        );
    }
    if (demand.graphics_cache) {
        auto snapshot = make_graphics_cache_snapshot(
            graphics_device->resource_cache_stats()
        );
        publish_snapshot_result(
            requests,
            commands,
            c_graphics_cache_capability,
            c_graphics_cache_schema,
            encode_json(Ref(snapshot)),
            ++state->graphics_cache_version
        );
    }
}

bool has_frame_demand(
    Query<Entity, const Request, const BlobRequest> requests,
    Query<const Subscription> subscriptions
) {
    for (auto [entity, request, blob_request] : requests) {
        (void)entity;
        (void)blob_request;
        if (request.capability == "rendering.frame") {
            return true;
        }
    }
    for (auto [subscription] : subscriptions) {
        if (subscription.capability == "rendering.frame") {
            return true;
        }
    }
    return false;
}

void respond_frame_errors(
    Query<Entity, const Request, const BlobRequest> requests,
    Commands commands,
    std::string message
) {
    for (auto [entity, request, blob_request] : requests) {
        (void)blob_request;
        if (request.capability != "rendering.frame") {
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
            .capability = "rendering.frame",
            .bytes = jpeg,
            .mime = "image/jpeg",
            .version = version,
            .metadata = metadata,
        }
    );

    for (auto [entity, request, blob_request] : requests) {
        (void)blob_request;
        if (request.capability != "rendering.frame") {
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

void capture_rendering_frame(
    Optional<ResRO<DeferredViewTargets>> targets,
    ResRW<FrameCaptureState> state,
    ResRO<Config> config,
    Query<Entity, const Request, const BlobRequest> requests,
    Query<const Subscription> subscriptions,
    Commands commands
) {
    if (auto completed = state->readback->poll()) {
        auto target = state->take_target(completed->user_data);
        auto jpeg = encode_jpeg(
            completed->data,
            completed->width,
            completed->height,
            config->jpeg_quality
        );
        if (jpeg.empty()) {
            respond_frame_errors(requests, commands, "JPEG encoding failed");
        } else {
            ++state->frame_version;
            publish_completed_frame(
                requests,
                commands,
                std::move(jpeg),
                completed->width,
                completed->height,
                std::move(target),
                state->frame_version
            );
        }
    }

    if (!has_frame_demand(requests, subscriptions)) {
        return;
    }

    auto now = std::chrono::steady_clock::now();
    if (!can_capture_now(*config, *state, now) ||
        !state->readback->can_enqueue()) {
        return;
    }

    auto target = select_frame_target(targets);
    if (!target.texture) {
        respond_frame_errors(
            requests,
            commands,
            "No render target texture is available"
        );
        return;
    }
    if (target.texture->type() != TextureType::Texture2D ||
        target.texture->format() != PixelFormat::Rgba8Unorm ||
        target.texture->depth() != 1) {
        respond_frame_errors(
            requests,
            commands,
            "DevTools frame capture requires a 2D Rgba8Unorm texture"
        );
        return;
    }

    auto user_data = state->remember_target(std::move(target.name));
    if (!state->readback->enqueue(
            TextureReadbackRequest {
                .texture = std::move(target.texture),
                .output_format = PixelFormat::Rgba8Unorm,
                .user_data = user_data,
            }
        )) {
        state->forget_target(user_data);
        respond_frame_errors(
            requests,
            commands,
            "Texture readback queue is full or unsupported"
        );
        return;
    }

    mark_capture_enqueued(*config, *state, now);
}

} // namespace

ProviderPlugin::ProviderPlugin(Config config) : m_config(config) {}

void ProviderPlugin::setup(App& app) {
    if (!app.has_resource<Bridge>()) {
        fatal(
            "devtools::rendering::ProviderPlugin requires "
            "devtools::CorePlugin. Add devtools::CorePlugin before "
            "devtools::rendering::ProviderPlugin."
        );
    }

    auto& registry = Registry::instance();
    if (!registry.try_get_cls(type_id<RenderSystemSnapshot>()) ||
        !registry.try_get_cls(type_id<RenderScheduleSnapshot>()) ||
        !registry.try_get_cls(type_id<ResourceSetSourceSnapshot>()) ||
        !registry.try_get_cls(type_id<GraphicsCacheSnapshot>())) {
        fatal(
            "devtools::rendering::ProviderPlugin requires ReflectionPlugin. "
            "Add ReflectionPlugin before "
            "devtools::rendering::ProviderPlugin."
        );
    }

    declare_capability(
        app.world(),
        "rendering.frame",
        "Rendered Frame",
        BlobCapability {
            .mime = "image/jpeg",
            .mode = PublishMode::OnDemand,
            .waitable = true,
        }
    );
    declare_capability(
        app.world(),
        c_render_schedule_capability,
        "Render Schedule",
        SnapshotCapability {
            .schema = c_render_schedule_schema,
            .data_type = type_id<RenderScheduleSnapshot>(),
            .mode = PublishMode::Cached,
        }
    );
    declare_capability(
        app.world(),
        "graphics.cache",
        "Graphics Cache",
        SnapshotCapability {
            .schema = c_graphics_cache_schema,
            .data_type = type_id<GraphicsCacheSnapshot>(),
            .mode = PublishMode::Cached,
        }
    );

    app.add_resource(Config {m_config});
    app.add_resource(SnapshotPublishState {});
    app.add_resource(
        FrameCaptureState {
            .readback =
                app.resource<GraphicsDevice>().create_texture_readback(),
        }
    );
    app.add_systems(RenderEnd, publish_rendering_snapshots);
    app.add_systems(RenderEnd, capture_rendering_frame);
}

void ProviderPlugin::finish(App&) {}

} // namespace fei::devtools::rendering
