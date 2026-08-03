#include "project/plugin.hpp"

#include "app/app.hpp"
#include "asset/plugin.hpp"
#include "base/log.hpp"

#include <utility>

namespace fei {

void ProjectPlugin::setup(App& app) {
    if (app.has_resource<Project>()) {
        fatal("A project is already loaded");
    }
    if (app.has_plugin<AssetsPlugin>()) {
        fatal("ProjectPlugin must be added before AssetsPlugin");
    }

    const auto asset_root = m_project.asset_root();
    app.add_resource(std::move(m_project));
    app.add_plugin(
        AssetsPlugin {
            AssetsPluginConfig {.project_asset_root = asset_root},
        }
    );
}

} // namespace fei
