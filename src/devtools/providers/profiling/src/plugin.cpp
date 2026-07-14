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
#include "snapshot_demand.hpp"
#include "snapshot_types.hpp"

#include <optional>
#include <string>

namespace fei::devtools::profiling {

namespace {

constexpr const char* c_frame_stats_schema = "profiling.frame_stats.v1";
constexpr const char* c_summary_schema = "profiling.summary.v1";
constexpr const char* c_frame_history_schema = "profiling.frame_history.v1";

struct PublishState {
    uint64 frame_stats_version {0};
    uint64 summary_version {0};
    uint64 frame_history_version {0};
};

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
                    .capability = capability,
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
    for (auto [entity, request, snapshot_request] : requests) {
        (void)snapshot_request;
        if (request.capability != capability) {
            continue;
        }
        commands.spawn().add(
            SnapshotResponse {
                .token = request.token,
                .capability = capability,
                .json = *json,
                .schema = schema,
                .version = version,
            }
        );
        commands.entity(entity).despawn();
    }
}

void publish_profiling_snapshots(
    ResRW<PublishState> state,
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

    std::optional<fei::ProfileSummarySnapshot> detailed;
    if (demand.detailed()) {
        detailed.emplace(fei::profile_summary_snapshot());
    }

    if (demand.frame_stats) {
        const auto stats =
            detailed ? detailed->frame_stats : fei::profile_frame_stats();
        auto snapshot = make_frame_stats_snapshot(stats);
        publish_snapshot_result(
            requests,
            commands,
            c_frame_stats_capability,
            c_frame_stats_schema,
            encode_json(Ref(snapshot)),
            ++state->frame_stats_version
        );
    }
    if (demand.summary) {
        auto snapshot = make_summary_snapshot(*detailed);
        publish_snapshot_result(
            requests,
            commands,
            c_summary_capability,
            c_summary_schema,
            encode_json(Ref(snapshot)),
            ++state->summary_version
        );
    }
    if (demand.frame_history) {
        auto snapshot = make_frame_history_snapshot(*detailed);
        publish_snapshot_result(
            requests,
            commands,
            c_frame_history_capability,
            c_frame_history_schema,
            encode_json(Ref(snapshot)),
            ++state->frame_history_version
        );
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
    auto& registry = Registry::instance();
    if (!registry.try_get_cls(type_id<FrameStatsSnapshot>()) ||
        !registry.try_get_cls(type_id<SummaryEntrySnapshot>()) ||
        !registry.try_get_cls(type_id<SummarySnapshot>()) ||
        !registry.try_get_cls(type_id<FrameHistorySampleSnapshot>()) ||
        !registry.try_get_cls(type_id<FrameHistorySnapshot>())) {
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
    declare_capability(
        app.world(),
        c_summary_capability,
        "Profiling Summary",
        SnapshotCapability {
            .schema = c_summary_schema,
            .data_type = type_id<SummarySnapshot>(),
            .mode = c_summary_mode,
        }
    );
    declare_capability(
        app.world(),
        c_frame_history_capability,
        "Frame History",
        SnapshotCapability {
            .schema = c_frame_history_schema,
            .data_type = type_id<FrameHistorySnapshot>(),
            .mode = c_frame_history_mode,
        }
    );
    app.add_resource(PublishState {});
    app.add_systems(PostUpdate, publish_profiling_snapshots);
}

void ProviderPlugin::finish(App&) {}

} // namespace fei::devtools::profiling
