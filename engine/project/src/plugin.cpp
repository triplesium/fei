#include "project/plugin.hpp"

#include "app/app.hpp"
#include "asset/plugin.hpp"
#include "base/log.hpp"

#include <utility>

namespace fei {

void ProjectPlugin::dependencies(PluginDependencies& dependencies) const {
    dependencies.require(
        AssetsPlugin {
            AssetsPluginConfig {
                .project_asset_root = m_project.asset_root(),
                .import_cache_root = m_project.imported_asset_root(),
            },
        }
    );
}

void ProjectPlugin::setup(App& app) {
    if (app.has_resource<Project>()) {
        fatal("A project is already loaded");
    }
    app.add_resource(std::move(m_project));
}

} // namespace fei
