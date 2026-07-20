#include "devtools_profiling/plugin.hpp"

#include "app/app.hpp"
#include "devtools/capability.hpp"
#include "ecs/commands.hpp"
#include "ecs/query.hpp"
#include "profiling/profiling.hpp"
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
    static constexpr ScheduleId schedule {PostUpdate};

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
    static constexpr ScheduleId schedule {PostUpdate};

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
    static constexpr ScheduleId schedule {PostUpdate};

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
    add_capabilities<FrameStats, ProfilingSummary, FrameHistory>(app);
}

void ProviderPlugin::finish(App&) {}

} // namespace fei::devtools::profiling
