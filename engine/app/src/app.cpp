#include "app/app.hpp"

#include "base/env.hpp"
#include "ecs/commands.hpp"
#include "profiling/profiling.hpp"

#include <chrono>
#include <cstdint>
#include <functional>
#include <stdexcept>
#include <string>

namespace ets {
namespace {

void register_main_schedule_profile_names() {
    register_profile_schedule_name(First, "First");
    register_profile_schedule_name(PreStartUp, "PreStartUp");
    register_profile_schedule_name(StartUp, "StartUp");
    register_profile_schedule_name(PreUpdate, "PreUpdate");
    register_profile_schedule_name(StateTransition, "StateTransition");
    register_profile_schedule_name(RunFixedMainLoop, "RunFixedMainLoop");
    register_profile_schedule_name(FixedFirst, "FixedFirst");
    register_profile_schedule_name(FixedPreUpdate, "FixedPreUpdate");
    register_profile_schedule_name(FixedUpdate, "FixedUpdate");
    register_profile_schedule_name(FixedPostUpdate, "FixedPostUpdate");
    register_profile_schedule_name(FixedLast, "FixedLast");
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
    ETS_PROFILE_DYNAMIC_SCOPE(name, __FILE__, __func__, __LINE__);
    app.run_schedule(schedule);
}

} // namespace

App::App() : m_runner(run_default) {
    add_resource<AppStates>();
    add_resource<CommandsQueue>();
    configure_sets(
        RunFixedMainLoop,
        chain(
            RunFixedMainLoopSystems::BeforeFixedMainLoop {},
            RunFixedMainLoopSystems::FixedMainLoop {},
            RunFixedMainLoopSystems::AfterFixedMainLoop {}
        )
    );
}

App::App(App&& other) noexcept :
    m_world(std::move(other.m_world)), m_plugins(std::move(other.m_plugins)),
    m_plugin_indices(std::move(other.m_plugin_indices)),
    m_plugin_order(std::move(other.m_plugin_order)),
    m_plugin_registry_frozen(other.m_plugin_registry_frozen),
    m_events(std::move(other.m_events)),
    m_sub_apps(std::move(other.m_sub_apps)),
    m_runner(std::move(other.m_runner)),
    m_relocation_handlers(std::move(other.m_relocation_handlers)),
    m_lifecycle(other.m_lifecycle) {
    for (auto& handler : m_relocation_handlers) {
        handler(*this);
    }
}

App& App::operator=(App&& other) noexcept {
    if (this == &other) {
        return *this;
    }
    m_world = std::move(other.m_world);
    m_plugins = std::move(other.m_plugins);
    m_plugin_indices = std::move(other.m_plugin_indices);
    m_plugin_order = std::move(other.m_plugin_order);
    m_plugin_registry_frozen = other.m_plugin_registry_frozen;
    m_events = std::move(other.m_events);
    m_sub_apps = std::move(other.m_sub_apps);
    m_runner = std::move(other.m_runner);
    m_relocation_handlers = std::move(other.m_relocation_handlers);
    m_lifecycle = other.m_lifecycle;
    for (auto& handler : m_relocation_handlers) {
        handler(*this);
    }
    return *this;
}

App& App::set_runner(AppRunner runner) {
    if (!runner) {
        throw std::invalid_argument("App runner cannot be empty");
    }
    m_runner = std::move(runner);
    return *this;
}

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

    m_plugin_registry_frozen = true;

    std::size_t resolved_count = 0;
    while (resolved_count < m_plugins.size()) {
        const auto wave_end = m_plugins.size();
        std::vector<PluginRequirement> missing;
        for (; resolved_count < wave_end; ++resolved_count) {
            PluginDependencies dependencies;
            m_plugins[resolved_count].plugin->dependencies(dependencies);
            m_plugins[resolved_count].requirements =
                dependencies.requirements();

            for (const auto& requirement :
                 m_plugins[resolved_count].requirements) {
                if (m_plugin_indices.contains(requirement.type)) {
                    continue;
                }
                auto existing = std::ranges::find_if(
                    missing,
                    [&](const PluginRequirement& candidate) {
                        return candidate.type == requirement.type;
                    }
                );
                if (existing == missing.end()) {
                    auto missing_requirement = requirement;
                    missing_requirement.required_by =
                        m_plugins[resolved_count].name;
                    missing.push_back(std::move(missing_requirement));
                } else if (requirement.configured && !existing->configured) {
                    *existing = requirement;
                    existing->required_by = m_plugins[resolved_count].name;
                }
            }
        }

        for (auto& requirement : missing) {
            if (m_plugin_indices.contains(requirement.type)) {
                continue;
            }
            if (!requirement.create_default) {
                throw std::runtime_error(
                    "Plugin " + requirement.required_by + " requires " +
                    requirement.name +
                    ", which must be registered explicitly because it is "
                    "not default constructible"
                );
            }
            const auto dependency_index = m_plugins.size();
            m_plugin_indices.emplace(requirement.type, dependency_index);
            m_plugins.push_back(
                PluginEntry {
                    .type = requirement.type,
                    .name = requirement.name,
                    .plugin = requirement.create_default(),
                }
            );
        }
    }

    enum class VisitState : std::uint8_t { Unvisited, Visiting, Visited };
    std::vector states(m_plugins.size(), VisitState::Unvisited);
    std::vector<std::size_t> stack;
    m_plugin_order.clear();
    m_plugin_order.reserve(m_plugins.size());

    std::function<void(std::size_t)> visit = [&](std::size_t index) {
        if (states[index] == VisitState::Visited) {
            return;
        }
        if (states[index] == VisitState::Visiting) {
            auto cycle_start = std::ranges::find(stack, index);
            std::string cycle;
            for (auto it = cycle_start; it != stack.end(); ++it) {
                if (!cycle.empty()) {
                    cycle += " -> ";
                }
                cycle += m_plugins[*it].name;
            }
            cycle += " -> " + m_plugins[index].name;
            throw std::runtime_error("Plugin dependency cycle: " + cycle);
        }

        states[index] = VisitState::Visiting;
        stack.push_back(index);
        for (const auto& requirement : m_plugins[index].requirements) {
            visit(m_plugin_indices.at(requirement.type));
        }
        stack.pop_back();
        states[index] = VisitState::Visited;
        m_plugin_order.push_back(index);
    };

    for (std::size_t index = 0; index < m_plugins.size(); ++index) {
        visit(index);
    }

    try {
        for (auto index : m_plugin_order) {
            auto& entry = m_plugins[index];
            entry.state = PluginState::SettingUp;
            entry.plugin->setup(*this);
            entry.state = PluginState::Setup;
        }

        register_main_schedule_profile_names();
        for (auto index : m_plugin_order) {
            auto& entry = m_plugins[index];
            entry.plugin->finish(*this);
            entry.state = PluginState::Finished;
        }
        m_world.sort_systems();
        for (auto& entry : m_sub_apps) {
            entry.runner->finish();
        }
        m_lifecycle = AppLifecycle::Ready;
    } catch (...) {
        shutdown();
        throw;
    }
}

void App::startup() {
    if (m_lifecycle == AppLifecycle::Building) {
        finish();
    }
    if (m_lifecycle != AppLifecycle::Ready) {
        return;
    }

    run_profiled_schedule(*this, StateTransition, "StateTransition");
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

    // Keep removal messages alive across update/render boundaries, then rotate
    // their double buffers at the start of the next main update.
    m_world.clear_trackers();

    run_profiled_schedule(*this, First, "First");
    run_profiled_schedule(*this, PreUpdate, "PreUpdate");
    run_profiled_schedule(*this, StateTransition, "StateTransition");
    run_profiled_schedule(*this, RunFixedMainLoop, "RunFixedMainLoop");
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
    ETS_PROFILE_FRAME();
}

void App::shutdown() noexcept {
    if (m_lifecycle == AppLifecycle::Stopped) {
        return;
    }

    for (auto runner = m_sub_apps.rbegin(); runner != m_sub_apps.rend();
         ++runner) {
        runner->runner->shutdown();
    }
    for (auto order = m_plugin_order.rbegin(); order != m_plugin_order.rend();
         ++order) {
        auto& entry = m_plugins[*order];
        if (entry.state == PluginState::Registered ||
            entry.state == PluginState::Cleaned) {
            continue;
        }
        entry.plugin->cleanup(*this);
        entry.state = PluginState::Cleaned;
    }
    m_lifecycle = AppLifecycle::Stopped;
}

void App::run_default(App&& app) {
    const auto exit_after_seconds =
        read_environment_variable<double>("ETS_EXIT_AFTER_SECONDS");
    const auto exit_after_frames =
        read_environment_variable<std::uint64_t>("ETS_EXIT_AFTER_FRAMES");
    const auto start_time = std::chrono::steady_clock::now();
    std::uint64_t frame_count = 0;

    try {
        app.startup();
        bool should_stop = false;
        while (!should_stop) {
            app.update();
            app.render();
            ++frame_count;

            auto& app_states = app.resource<AppStates>();
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
        app.shutdown();
        throw;
    }
    app.shutdown();
}

void App::run() {
    if (m_lifecycle == AppLifecycle::Stopped) {
        return;
    }
    if (m_lifecycle == AppLifecycle::Building) {
        finish();
    }
    if (!m_runner) {
        throw std::logic_error("App runner has already been consumed");
    }

    auto runner = std::move(m_runner);
    runner(std::move(*this));
}
} // namespace ets
