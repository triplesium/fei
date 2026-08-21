#include "asset/plugin.hpp"

#include "asset/database.hpp"
#include "asset/importer.hpp"
#include "asset/request.hpp"
#include "asset/systems.hpp"
#include "base/log.hpp"

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
#ifdef __EMSCRIPTEN__
    std::error_code asset_root_error;
    std::filesystem::create_directories(project_asset_root, asset_root_error);
    if (asset_root_error) {
        warn(
            "Failed to create browser asset root: {}",
            asset_root_error.message()
        );
    }
#endif
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
    app.add_relocation_handler([](App& relocated) {
        relocated.resource<AssetServer>().rebind_app(&relocated);
    });
}

} // namespace fei
