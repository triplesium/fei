#include "asset/plugin.hpp"

#include "asset/request.hpp"
#include "asset/systems.hpp"
#include "task/plugin.hpp"

namespace fei {

namespace {

std::filesystem::path default_project_asset_root() {
#ifdef FEI_ASSETS_PATH
    return FEI_ASSETS_PATH;
#else
    return std::filesystem::current_path();
#endif
}

} // namespace

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

    auto project_asset_root = m_config.project_asset_root.empty() ?
                                  default_project_asset_root() :
                                  m_config.project_asset_root;
    AssetServer server {&app, "project"};
    server.emplace_source<FilesystemAssetSource>(
        "project",
        std::move(project_asset_root)
    );
    server.emplace_source<EmbeddedAssetSource>();
    app.add_resource(std::move(server));
}

} // namespace fei
