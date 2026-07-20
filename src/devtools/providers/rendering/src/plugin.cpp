#include "devtools_rendering/plugin.hpp"

#include "app/app.hpp"
#include "devtools/capability.hpp"
#include "ecs/commands.hpp"
#include "ecs/query.hpp"
#include "ecs/system_params.hpp"
#include "ecs/world.hpp"
#include "graphics/graphics_device.hpp"
#include "snapshot_types.hpp"

#include <string_view>

namespace fei::devtools::rendering {

namespace {

struct RenderSchedule {
    using RequestBody = void;
    using ResponseBody = RenderScheduleSnapshot;

    static constexpr std::string_view id {"rendering.render_schedule"};
    static constexpr std::string_view label {"Render Schedule"};
    static constexpr std::string_view schema {"rendering.render_schedule.v1"};
    static constexpr ScheduleId schedule {RenderEnd};

    static void
    run(Query<Entity, const Request, const JsonRequest> requests,
        Commands commands) {
        for (auto [entity, request, json] : requests) {
            (void)json;
            if (request.capability != id) {
                continue;
            }

            ResponseBody response;
            if (auto debug =
                    commands.world().schedule_debug_info(RenderUpdate)) {
                response = make_render_schedule_snapshot(*debug);
            }
            respond_capability(commands, entity, request, response);
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
    add_capabilities<RenderSchedule, GraphicsCache>(app);
}

void ProviderPlugin::finish(App&) {}

} // namespace fei::devtools::rendering
