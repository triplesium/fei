#include "devtools_profiling/plugin.hpp"

#include "app/app.hpp"
#include "base/log.hpp"
#include "devtools/bridge.hpp"
#include "devtools/capability.hpp"
#include "devtools/json.hpp"
#include "ecs/commands.hpp"
#include "ecs/query.hpp"
#include "ecs/system_params.hpp"
#include "profiling/profiling.hpp"
#include "refl/registry.hpp"
#include "snapshot_types.hpp"

#include <string>

namespace fei::devtools::profiling {

namespace {

constexpr const char* c_frame_stats_capability = "profiling.frame_stats";
constexpr const char* c_frame_stats_schema = "profiling.frame_stats.v1";

struct PublishState {
    uint64 version {0};
};

void publish_frame_stats(
    ResRW<PublishState> state,
    Query<Entity, const Request, const SnapshotRequest> requests,
    Commands commands
) {
    bool requested = false;
    for (auto [entity, request, snapshot_request] : requests) {
        (void)entity;
        (void)snapshot_request;
        if (request.capability == c_frame_stats_capability) {
            requested = true;
            break;
        }
    }
    if (!requested) {
        return;
    }

    auto snapshot = make_frame_stats_snapshot(profile_frame_stats());
    auto json = encode_json(Ref(snapshot));
    if (!json) {
        error(
            "Failed to encode DevTools snapshot {}: {}",
            c_frame_stats_capability,
            json.error()
        );
    }

    const auto version = ++state->version;
    if (json) {
        commands.spawn().add(
            SnapshotResponse {
                .capability = c_frame_stats_capability,
                .json = *json,
                .schema = c_frame_stats_schema,
                .version = version,
            }
        );
    }

    for (auto [entity, request, snapshot_request] : requests) {
        (void)snapshot_request;
        if (request.capability != c_frame_stats_capability) {
            continue;
        }
        if (json) {
            commands.spawn().add(
                SnapshotResponse {
                    .token = request.token,
                    .capability = c_frame_stats_capability,
                    .json = *json,
                    .schema = c_frame_stats_schema,
                    .version = version,
                }
            );
        } else {
            commands.spawn().add(
                ErrorResponse {
                    .token = request.token,
                    .capability = c_frame_stats_capability,
                    .status = 500,
                    .message = json.error(),
                }
            );
        }
        commands.entity(entity).despawn();
    }
}

} // namespace

void ProviderPlugin::setup(App& app) {
    if (!app.has_resource<Bridge>()) {
        fatal(
            "devtools::profiling::ProviderPlugin requires "
            "devtools::CorePlugin. Add devtools::CorePlugin before "
            "devtools::profiling::ProviderPlugin."
        );
    }
    if (!Registry::instance().try_get_cls(type_id<FrameStatsSnapshot>())) {
        fatal(
            "devtools::profiling::ProviderPlugin requires ReflectionPlugin. "
            "Add ReflectionPlugin before "
            "devtools::profiling::ProviderPlugin."
        );
    }

    declare_capability(
        app.world(),
        c_frame_stats_capability,
        "Frame Statistics",
        SnapshotCapability {
            .schema = c_frame_stats_schema,
            .data_type = type_id<FrameStatsSnapshot>(),
            .mode = PublishMode::Cached,
        }
    );
    app.add_resource(PublishState {});
    app.add_systems(PostUpdate, publish_frame_stats);
}

void ProviderPlugin::finish(App&) {}

} // namespace fei::devtools::profiling
