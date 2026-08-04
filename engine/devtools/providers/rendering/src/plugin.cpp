#include "devtools_rendering/plugin.hpp"

#include "app/app.hpp"
#include "devtools/capability.hpp"
#include "ecs/commands.hpp"
#include "ecs/query.hpp"
#include "ecs/system_params.hpp"
#include "ecs/world.hpp"
#include "graphics/graphics_device.hpp"
#include "rendering/render_app.hpp"
#include "snapshot_types.hpp"

#include <string_view>

namespace fei::devtools::rendering {

namespace {

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

void ProviderPlugin::setup(App& app) {
    app.add_resource(RenderScheduleState {});
    add_capability<RenderSchedule>(app);
    declare_capability<GraphicsCache>(app.world());
    add_extract_component<Request>(app);
    add_extract_component<JsonRequest>(app);
    add_render_to_main_component<JsonResponse>(app);
    add_render_to_main_component<ErrorResponse>(app);
    app.sub_app<RenderApp>()
        .add_resource(RenderScheduleState {})
        .add_systems(
            GraphicsCache::schedule,
            capture_render_schedule,
            GraphicsCache::run
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
