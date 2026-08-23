#include "project_runtime/runtime.hpp"

#include "project/plugin.hpp"

#include <utility>

namespace ets {

void configure_project_runtime(App& app, Project project) {
    const auto runtime = project.config().runtime;
    app.add_plugin(ProjectPlugin {std::move(project)});
    for (const auto& plugin : runtime.plugins) {
        app.add_plugin(plugin);
    }
}

} // namespace ets
