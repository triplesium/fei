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
#include "refl/registry.hpp"
#include "snapshot_demand.hpp"
#include "snapshot_types.hpp"

#include <string>

namespace fei::devtools::rendering {

namespace {

constexpr const char* c_render_schedule_schema = "rendering.render_schedule.v1";
constexpr const char* c_graphics_cache_schema = "graphics.cache.v2";

struct SnapshotPublishState {
    uint64 render_schedule_version {0};
    uint64 graphics_cache_version {0};
};

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

} // namespace

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

    app.add_resource(SnapshotPublishState {});
    app.add_systems(RenderEnd, publish_rendering_snapshots);
}

void ProviderPlugin::finish(App&) {}

} // namespace fei::devtools::rendering
