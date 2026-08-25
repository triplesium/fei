#include "project_runtime/runtime.hpp"

#include "project/plugin.hpp"

#include <utility>

namespace ets {

void configure_project_runtime(App& app, Project project) {
    app.add_plugin(ProjectPlugin {std::move(project)});
}

} // namespace ets
