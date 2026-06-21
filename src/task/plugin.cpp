#include "task/plugin.hpp"

#include "app/app.hpp"
#include "ecs/system_config.hpp"
#include "ecs/system_params.hpp"

namespace fei {

Tasks::Tasks(std::size_t thread_count) :
    m_general(std::make_unique<TaskPool>(thread_count)) {}

std::size_t Tasks::drain_completions() {
    return m_general->drain_completions();
}

void Tasks::drain_completion_system(Res<Tasks> tasks) {
    tasks->drain_completions();
}

void TaskPlugin::setup(App& app) {
    if (!app.has_resource<Tasks>()) {
        app.add_resource(Tasks {});
    }
    app.add_systems(
        PostUpdate,
        Tasks::drain_completion_system | in_set<TaskSystems::DrainCompletions>()
    );
}

} // namespace fei
