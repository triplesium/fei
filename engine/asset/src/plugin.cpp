#include "asset/plugin.hpp"

#include "asset/database.hpp"
#include "asset/importer.hpp"
#include "asset/request.hpp"
#include "asset/systems.hpp"
#include "base/log.hpp"
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

std::filesystem::path
default_import_cache_root(const std::filesystem::path& project_asset_root) {
    return project_asset_root.parent_path() / ".fei" / "imported";
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
    auto import_cache_root = m_config.import_cache_root.empty() ?
                                 default_import_cache_root(project_asset_root) :
                                 m_config.import_cache_root;
    AssetDatabase database(project_asset_root, import_cache_root);
    if (auto status = database.scan(); !status) {
        warn("Failed to scan project asset metadata: {}", status.error());
    }

    AssetServer server {&app, "project"};
    server.emplace_source<FilesystemAssetSource>("project", project_asset_root);
    server.emplace_source<EmbeddedAssetSource>();
    app.add_resource(std::move(server))
        .add_resource(AssetImporterRegistry {})
        .add_resource(std::move(database));
}

} // namespace fei
