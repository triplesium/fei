#include "asset/plugin.hpp"

#include "asset/request.hpp"
#include "asset/systems.hpp"
#include "task/plugin.hpp"

namespace fei {

void AssetsPlugin::setup(App& app) {
    if (!app.has_plugin<TaskPlugin>()) {
        app.add_plugin<TaskPlugin>();
    }

    app.configure_sets(
        PostUpdate,
        chain(
            AssetSystems::ProcessLoadRequests {},
            TaskSystems::DrainCompletions {},
            AssetSystems::ApplyAsyncLoads {},
            AssetSystems::CollectUnused {},
            AssetSystems::TrackAssets {}
        )
    );

    if (!app.has_resource<AssetLoadRequests>()) {
        app.add_resource(AssetLoadRequests {});
    }
    app.add_systems(
        PostUpdate,
        AssetLoadRequests::process_system |
            in_set<AssetSystems::ProcessLoadRequests>()
    );

    AssetServer server {&app};
    server.emplace_source<DefaultAssetSource>();
    server.emplace_source<EmbededAssetSource>();
    app.add_resource(std::move(server));
}

} // namespace fei
