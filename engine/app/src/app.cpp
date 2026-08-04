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

void run_profiled_schedule(App& app, ScheduleId schedule, const char* name) {
    FEI_PROFILE_DYNAMIC_SCOPE(name, __FILE__, __func__, __LINE__);
    app.run_schedule(schedule);
}

} // namespace

App& App::add_plugins(PluginGroupBuilder builder) {
    builder.finish(*this);
    return *this;
}

SubAppSource App::resolve_sub_app_source(LabeledSubApp& entry) {
    auto source = entry.source_selector ? entry.source_selector(m_world) :
                                          SubAppSource {&m_world, 0};
    if (source.world == nullptr) {
        fatal("SubApp source selector returned a null World");
    }
    return source;
}

void App::finish() {
    if (m_lifecycle != AppLifecycle::Building) {
        return;
    }

    register_main_schedule_profile_names();
    for (auto& plugin : m_plugins) {
        plugin->finish(*this);
    }
    m_world.sort_systems();
    for (auto& entry : m_sub_apps) {
        entry.runner->finish();
    }
    m_lifecycle = AppLifecycle::Ready;
}

void App::startup() {
    if (m_lifecycle == AppLifecycle::Building) {
        finish();
    }
    if (m_lifecycle != AppLifecycle::Ready) {
        return;
    }

    run_profiled_schedule(*this, PreStartUp, "PreStartUp");
    run_profiled_schedule(*this, StartUp, "StartUp");
    for (auto& entry : m_sub_apps) {
        entry.runner->startup(resolve_sub_app_source(entry));
    }
    m_lifecycle = AppLifecycle::Running;
}

void App::update() {
    if (m_lifecycle == AppLifecycle::Building ||
        m_lifecycle == AppLifecycle::Ready) {
        startup();
    }
    if (m_lifecycle != AppLifecycle::Running) {
        return;
    }

    run_profiled_schedule(*this, First, "First");
    run_profiled_schedule(*this, PreUpdate, "PreUpdate");
    run_profiled_schedule(*this, StateTransitionSchedule, "StateTransition");
    run_profiled_schedule(*this, Update, "Update");
    run_profiled_schedule(*this, PostUpdate, "PostUpdate");
    run_profiled_schedule(*this, Last, "Last");
}

void App::render() {
    if (m_lifecycle == AppLifecycle::Building ||
        m_lifecycle == AppLifecycle::Ready) {
        startup();
    }
    if (m_lifecycle != AppLifecycle::Running) {
        return;
    }

    run_profiled_schedule(*this, RenderPrepare, "RenderPrepare");
    run_profiled_schedule(*this, RenderFirst, "RenderFirst");
    run_profiled_schedule(*this, RenderStart, "RenderStart");
    run_profiled_schedule(*this, RenderUpdate, "RenderUpdate");
    run_profiled_schedule(*this, RenderEnd, "RenderEnd");
    run_profiled_schedule(*this, RenderLast, "RenderLast");
    for (auto& entry : m_sub_apps) {
        entry.runner->update(resolve_sub_app_source(entry));
    }
    FEI_PROFILE_FRAME();
}

void App::shutdown() noexcept {
    if (m_lifecycle == AppLifecycle::Stopped) {
        return;
    }

    for (auto runner = m_sub_apps.rbegin(); runner != m_sub_apps.rend();
         ++runner) {
        runner->runner->shutdown();
    }
    for (auto plugin = m_plugins.rbegin(); plugin != m_plugins.rend();
         ++plugin) {
        (*plugin)->cleanup(*this);
    }
    m_lifecycle = AppLifecycle::Stopped;
}

void App::run() {
    if (m_lifecycle == AppLifecycle::Stopped) {
        return;
    }

    const auto exit_after_seconds =
        read_environment_variable<double>("FEI_EXIT_AFTER_SECONDS");
    const auto exit_after_frames =
        read_environment_variable<std::uint64_t>("FEI_EXIT_AFTER_FRAMES");
    const auto start_time = std::chrono::steady_clock::now();
    std::uint64_t frame_count = 0;

    try {
        startup();
        bool should_stop = false;
        while (!should_stop) {
            update();
            render();
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
        shutdown();
        throw;
    }
    shutdown();
}
} // namespace fei
