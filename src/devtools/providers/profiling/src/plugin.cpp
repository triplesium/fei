#include "devtools_profiling/plugin.hpp"

#include "app/app.hpp"
#include "base/log.hpp"
#include "devtools/bridge.hpp"
#include "devtools/capability.hpp"
#include "ecs/commands.hpp"
#include "ecs/query.hpp"
#include "profiling/profiling.hpp"
#include "refl/registry.hpp"
#include "snapshot_types.hpp"

#include <string_view>

namespace fei::devtools::profiling {

namespace {

struct FrameStats {
    using RequestBody = void;
    using ResponseBody = FrameStatsSnapshot;

    static constexpr std::string_view id {"profiling.frame_stats"};
    static constexpr std::string_view label {"Frame Statistics"};
    static constexpr std::string_view schema {"profiling.frame_stats.v1"};

    static void
    run(Query<Entity, const Request, const JsonRequest> requests,
        Commands commands) {
        for (auto [entity, request, json] : requests) {
            (void)json;
            if (request.capability != id) {
                continue;
            }

            auto response =
                make_frame_stats_snapshot(fei::profile_frame_stats());
            respond_capability(commands, entity, request, response);
        }
    }
};

struct ProfilingSummary {
    using RequestBody = void;
    using ResponseBody = SummarySnapshot;

    static constexpr std::string_view id {"profiling.summary"};
    static constexpr std::string_view label {"Profiling Summary"};
    static constexpr std::string_view schema {"profiling.summary.v1"};

    static void
    run(Query<Entity, const Request, const JsonRequest> requests,
        Commands commands) {
        for (auto [entity, request, json] : requests) {
            (void)json;
            if (request.capability != id) {
                continue;
            }

            auto source = fei::profile_summary_snapshot();
            auto response = make_summary_snapshot(source);
            respond_capability(commands, entity, request, response);
        }
    }
};

struct FrameHistory {
    using RequestBody = void;
    using ResponseBody = FrameHistorySnapshot;

    static constexpr std::string_view id {"profiling.frame_history"};
    static constexpr std::string_view label {"Frame History"};
    static constexpr std::string_view schema {"profiling.frame_history.v1"};

    static void
    run(Query<Entity, const Request, const JsonRequest> requests,
        Commands commands) {
        for (auto [entity, request, json] : requests) {
            (void)json;
            if (request.capability != id) {
                continue;
            }

            auto source = fei::profile_summary_snapshot();
            auto response = make_frame_history_snapshot(source);
            respond_capability(commands, entity, request, response);
        }
    }
};

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

    declare_capability<FrameStats>(app.world());
    declare_capability<ProfilingSummary>(app.world());
    declare_capability<FrameHistory>(app.world());
    app.add_systems(
        PostUpdate,
        FrameStats::run,
        ProfilingSummary::run,
        FrameHistory::run
    );
}

void ProviderPlugin::finish(App&) {}

} // namespace fei::devtools::profiling
