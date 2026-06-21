#include "asset/plugin.hpp"

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
            TaskSystems::DrainCompletions {},
            AssetSystems::ApplyAsyncLoads {},
            AssetSystems::TrackAssets {}
        )
    );

    AssetServer server {&app};
    server.emplace_source<DefaultAssetSource>();
    server.emplace_source<EmbededAssetSource>();
    app.add_resource(std::move(server));
}

} // namespace fei
