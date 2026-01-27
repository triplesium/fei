#include "app/app.hpp"

#include "ecs/commands.hpp"

#include <print>

namespace fei {
void App::run() {
    for (auto& [type, plugin] : m_plugins) {
        plugin->finish(*this);
    }
    m_world.sort_systems();

    run_schedule(PreStartUp);
    run_schedule(StartUp);
    resource<CommandsQueue>().execute(m_world);
    bool should_stop = false;
    while (!should_stop) {
        run_schedule(First);
        run_schedule(PreUpdate);
        run_schedule(Update);
        run_schedule(PostUpdate);
        run_schedule(Last);

        resource<CommandsQueue>().execute(m_world);

        run_schedule(RenderPrepare);
        run_schedule(RenderFirst);
        run_schedule(RenderStart);
        run_schedule(RenderUpdate);
        run_schedule(RenderEnd);
        run_schedule(RenderLast);

        auto& app_states = m_world.resource<AppStates>();
        should_stop = app_states.should_stop;
    }
}
} // namespace fei
