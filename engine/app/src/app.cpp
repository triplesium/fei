#include "app/app.hpp"

#include "base/env.hpp"
#include "profiling/profiling.hpp"

#include <chrono>
#include <cstdint>

namespace fei {
namespace {

void register_main_schedule_profile_names() {
    register_profile_schedule_name(First, "First");
    register_profile_schedule_name(PreStartUp, "PreStartUp");
    register_profile_schedule_name(StartUp, "StartUp");
    register_profile_schedule_name(PreUpdate, "PreUpdate");
    register_profile_schedule_name(StateTransitionSchedule, "StateTransition");
    register_profile_schedule_name(Update, "Update");
    register_profile_schedule_name(PostUpdate, "PostUpdate");
    register_profile_schedule_name(Last, "Last");
    register_profile_schedule_name(RenderPrepare, "RenderPrepare");
    register_profile_schedule_name(RenderFirst, "RenderFirst");
    register_profile_schedule_name(RenderStart, "RenderStart");
    register_profile_schedule_name(RenderUpdate, "RenderUpdate");
    register_profile_schedule_name(RenderEnd, "RenderEnd");
    register_profile_schedule_name(RenderLast, "RenderLast");
}

} // namespace

App& App::add_plugins(PluginGroupBuilder builder) {
    builder.finish(*this);
    return *this;
}

void App::run() {
    register_main_schedule_profile_names();
    const auto exit_after_seconds =
        read_environment_variable<double>("FEI_EXIT_AFTER_SECONDS");
    const auto exit_after_frames =
        read_environment_variable<std::uint64_t>("FEI_EXIT_AFTER_FRAMES");
    const auto start_time = std::chrono::steady_clock::now();
    std::uint64_t frame_count = 0;

    const auto cleanup_plugins = [this]() noexcept {
        for (auto plugin = m_plugins.rbegin(); plugin != m_plugins.rend();
             ++plugin) {
            (*plugin)->cleanup(*this);
        }
    };

#define FEI_RUN_PROFILED_SCHEDULE(schedule) \
    do {                                    \
        FEI_PROFILE_SCOPE(#schedule);       \
        run_schedule(schedule);             \
    } while (false)

    try {
        for (auto& plugin : m_plugins) {
            plugin->finish(*this);
        }
        m_world.sort_systems();

        FEI_RUN_PROFILED_SCHEDULE(PreStartUp);
        FEI_RUN_PROFILED_SCHEDULE(StartUp);
        bool should_stop = false;
        while (!should_stop) {
            FEI_RUN_PROFILED_SCHEDULE(First);
            FEI_RUN_PROFILED_SCHEDULE(PreUpdate);
            FEI_RUN_PROFILED_SCHEDULE(StateTransitionSchedule);
            FEI_RUN_PROFILED_SCHEDULE(Update);
            FEI_RUN_PROFILED_SCHEDULE(PostUpdate);
            FEI_RUN_PROFILED_SCHEDULE(Last);

            FEI_RUN_PROFILED_SCHEDULE(RenderPrepare);
            FEI_RUN_PROFILED_SCHEDULE(RenderFirst);
            FEI_RUN_PROFILED_SCHEDULE(RenderStart);
            FEI_RUN_PROFILED_SCHEDULE(RenderUpdate);
            FEI_RUN_PROFILED_SCHEDULE(RenderEnd);
            FEI_RUN_PROFILED_SCHEDULE(RenderLast);
            FEI_PROFILE_FRAME();
            ++frame_count;

            auto& app_states = m_world.resource<AppStates>();
            if (exit_after_frames && frame_count >= *exit_after_frames) {
                app_states.should_stop = true;
            }
            if (exit_after_seconds) {
                const auto elapsed = std::chrono::duration<double>(
                    std::chrono::steady_clock::now() - start_time
                );
                if (elapsed.count() >= *exit_after_seconds) {
                    app_states.should_stop = true;
                }
            }
            should_stop = app_states.should_stop;
        }
    } catch (...) {
        cleanup_plugins();
        throw;
    }
    cleanup_plugins();

#undef FEI_RUN_PROFILED_SCHEDULE
}
} // namespace fei
