#include "devtools_rendering/plugin.hpp"

#include "app/app.hpp"
#include "base/log.hpp"
#include "devtools/capability.hpp"
#include "ecs/commands.hpp"
#include "ecs/query.hpp"
#include "ecs/system_params.hpp"
#include "ecs/world.hpp"
#include "frame_capture.hpp"
#include "graphics/graphics_device.hpp"
#include "graphics/swapchain.hpp"
#include "rendering/extract_resource.hpp"
#include "rendering/plugin.hpp"
#include "rendering/render_app.hpp"
#include "snapshot_types.hpp"

#include <chrono>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace fei::devtools::rendering {

namespace {

constexpr std::string_view c_frame_capability {"rendering.frame"};

struct FrameCaptureSystems {
    struct Capture : SystemSet<Capture> {};
};

bool has_frame_demand(
    Query<Entity, const Request, const BlobRequest> requests,
    Query<const Subscription> subscriptions
) {
    for (auto [entity, request, blob] : requests) {
        (void)entity;
        (void)blob;
        if (request.capability == c_frame_capability) {
            return true;
        }
    }
    for (auto [subscription] : subscriptions) {
        if (subscription.capability == c_frame_capability) {
            return true;
        }
    }
    return false;
}

void respond_frame_error(
    Query<Entity, const Request, const BlobRequest> requests,
    Commands commands,
    std::string message
) {
    for (auto [entity, request, blob] : requests) {
        (void)blob;
        if (request.capability != c_frame_capability) {
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

void publish_frame(
    Query<Entity, const Request, const BlobRequest> requests,
    Commands commands,
    std::vector<byte> jpeg,
    uint32 width,
    uint32 height,
    uint64 version
) {
    std::unordered_map<std::string, std::string> metadata {
        {"width", std::to_string(width)},
        {"height", std::to_string(height)},
        {"source", "main_swapchain"},
    };
    commands.spawn().add(
        BlobResponse {
            .capability = std::string(c_frame_capability),
            .bytes = jpeg,
            .mime = "image/jpeg",
            .version = version,
            .metadata = metadata,
        }
    );

    for (auto [entity, request, blob] : requests) {
        (void)blob;
        if (request.capability != c_frame_capability) {
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

void capture_final_frame(
    ResRO<GraphicsDevice> device,
    Optional<ResRO<MainSwapchain>> main_swapchain,
    ResRO<Config> config,
    ResRW<FrameCaptureState> state,
    Query<Entity, const Request, const BlobRequest> requests,
    Query<const Subscription> subscriptions,
    Commands commands
) {
    if (!has_frame_demand(requests, subscriptions)) {
        return;
    }

    const auto now = std::chrono::steady_clock::now();
    if (!can_capture_now(*config, *state, now)) {
        return;
    }
    mark_capture_attempted(*config, *state, now);

    if (!main_swapchain || !(*main_swapchain)->swapchain) {
        respond_frame_error(
            requests,
            commands,
            "The renderer has no main swapchain to capture"
        );
        return;
    }

    auto captured =
        device->capture_presented_frame(*(*main_swapchain)->swapchain);
    if (!captured) {
        respond_frame_error(requests, commands, std::move(captured.error()));
        return;
    }

    auto frame = std::move(captured).value();
    auto jpeg = encode_jpeg(frame, config->jpeg_quality);
    if (jpeg.empty()) {
        respond_frame_error(
            requests,
            commands,
            "The captured swapchain frame could not be encoded as JPEG"
        );
        return;
    }

    publish_frame(
        requests,
        commands,
        std::move(jpeg),
        frame.width,
        frame.height,
        ++state->version
    );
}

struct RenderScheduleState {
    RenderScheduleSnapshot snapshot;
};

void capture_render_schedule(ResRW<RenderScheduleState> state, WorldRef world) {
    state->snapshot = {};
    if (auto debug = world->schedule_debug_info(RenderUpdate)) {
        state->snapshot = make_render_schedule_snapshot(*debug);
    }
}

struct RenderSchedule {
    using RequestBody = void;
    using ResponseBody = RenderScheduleSnapshot;

    static constexpr std::string_view id {"rendering.render_schedule"};
    static constexpr std::string_view label {"Render Schedule"};
    static constexpr std::string_view schema {"rendering.render_schedule.v1"};
    static constexpr ScheduleId schedule {RenderEnd};

    static void
    run(ResRO<RenderScheduleState> state,
        Query<Entity, const Request, const JsonRequest> requests,
        Commands commands) {
        for (auto [entity, request, json] : requests) {
            (void)json;
            if (request.capability != id) {
                continue;
            }

            respond_capability(commands, entity, request, state->snapshot);
        }
    }
};

struct GraphicsCache {
    using RequestBody = void;
    using ResponseBody = GraphicsCacheSnapshot;

    static constexpr std::string_view id {"graphics.cache"};
    static constexpr std::string_view label {"Graphics Cache"};
    static constexpr std::string_view schema {"graphics.cache.v2"};
    static constexpr ScheduleId schedule {RenderEnd};

    static void
    run(ResRO<GraphicsDevice> graphics_device,
        Query<Entity, const Request, const JsonRequest> requests,
        Commands commands) {
        for (auto [entity, request, json] : requests) {
            (void)json;
            if (request.capability != id) {
                continue;
            }

            auto response = make_graphics_cache_snapshot(
                graphics_device->resource_cache_stats()
            );
            respond_capability(commands, entity, request, response);
        }
    }
};

} // namespace

ProviderPlugin::ProviderPlugin(Config config) : m_config(config) {}

void ProviderPlugin::setup(App& app) {
    if (!app.has_resource<Bridge>()) {
        fatal(
            "DevTools capability '{}' requires devtools::CorePlugin. Add "
            "devtools::CorePlugin before its provider.",
            c_frame_capability
        );
    }

    app.add_resource(RenderScheduleState {});
    add_capability<RenderSchedule>(app);
    declare_capability<GraphicsCache>(app.world());
    declare_capability(
        app.world(),
        std::string(c_frame_capability),
        "Rendered Frame",
        BlobProtocol {
            .mime = "image/jpeg",
            .mode = PublishMode::OnDemand,
            .waitable = true,
        }
    );
    add_extract_component<Request>(app);
    add_extract_component<JsonRequest>(app);
    add_extract_component<BlobRequest>(app);
    add_extract_component<Subscription>(app);
    add_render_to_main_component<JsonResponse>(app);
    add_render_to_main_component<ErrorResponse>(app);
    add_render_to_main_component<BlobResponse>(app);
    app.add_resource(Config {m_config});
    add_extract_resource<Config>(app);

    app.sub_app<RenderApp>()
        .add_resource(RenderScheduleState {})
        .add_resource(FrameCaptureState {})
        .configure_sets(
            RenderLast,
            FrameCaptureSystems::Capture {}.before<RenderingSystems::Present>()
        )
        .add_systems(
            GraphicsCache::schedule,
            capture_render_schedule,
            GraphicsCache::run
        )
        .add_systems(
            RenderLast,
            capture_final_frame | in_set<FrameCaptureSystems::Capture>() |
                main_thread()
        )
        .add_post_update([](World& main_world, World& render_world) {
            main_world.resource<RenderScheduleState>().snapshot =
                static_cast<const World&>(render_world)
                    .resource<RenderScheduleState>()
                    .snapshot;
        });
}

void ProviderPlugin::finish(App&) {}

} // namespace fei::devtools::rendering
