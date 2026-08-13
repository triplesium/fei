#pragma once
#include "app/plugin_group.hpp"
#include "app/sub_app_runner.hpp"
#include "base/log.hpp"
#include "ecs/commands.hpp"
#include "ecs/event.hpp"
#include "ecs/state.hpp"
#include "ecs/system_params.hpp"
#include "ecs/world.hpp"
#include "refl/reflect.hpp"
#include "refl/type.hpp"

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace fei {

class PluginId;

enum MainSchedules : std::uint32_t {
    First,
    PreStartUp,
    StartUp,
    PreUpdate,
    Update,
    PostUpdate,
    Last,

    RenderPrepare,

    RenderFirst,
    RenderStart,
    RenderUpdate,
    RenderEnd,
    RenderLast
};

enum class AppLifecycle : std::uint8_t {
    Building,
    Ready,
    Running,
    Stopped,
};

FEI_REFLECT()
struct AppStates {
    bool should_stop {false};
};

template<typename E>
void event_update_system(ResRW<Events<E>> events) {
    events->update();
}

class App {
  private:
    friend class PluginGroupBuilder;

    enum class PluginState : std::uint8_t {
        Registered,
        SettingUp,
        Setup,
        Finished,
        Cleaned,
    };

    struct PluginEntry {
        TypeId type;
        std::string name;
        std::unique_ptr<Plugin> plugin;
        std::vector<PluginRequirement> requirements;
        PluginState state {PluginState::Registered};
    };

    struct LabeledSubApp {
        TypeId label;
        std::unique_ptr<SubAppRunner> runner;
        std::move_only_function<SubAppSource(World&)> source_selector;
    };

    // Declared first so the Main World outlives plugins and SubApps. Render
    // resources may hold explicit read-only references to Main World services.
    World m_world;
    std::vector<PluginEntry> m_plugins;
    std::unordered_map<TypeId, std::size_t> m_plugin_indices;
    std::vector<std::size_t> m_plugin_order;
    bool m_plugin_registry_frozen {false};
    std::unordered_set<TypeId> m_events;
    std::vector<LabeledSubApp> m_sub_apps;

    App& add_boxed_plugin(
        TypeId plugin_type,
        std::string_view plugin_name,
        std::unique_ptr<Plugin> plugin
    ) {
        if (m_lifecycle != AppLifecycle::Building || m_plugin_registry_frozen) {
            fatal(
                "Cannot add plugin {} after plugin dependency resolution has "
                "started",
                plugin_name
            );
        }
        if (!plugin) {
            fatal("Cannot add null plugin {}", plugin_name);
        }
        if (m_plugin_indices.contains(plugin_type)) {
            fatal("Plugin {} has already been added", plugin_name);
        }

        m_plugin_indices.emplace(plugin_type, m_plugins.size());
        m_plugins.push_back(
            PluginEntry {
                .type = plugin_type,
                .name = std::string(plugin_name),
                .plugin = std::move(plugin),
            }
        );
        return *this;
    }

  public:
    App() {
        add_resource<AppStates>();
        add_resource<CommandsQueue>();
    }

    template<typename E>
    App& add_event() {
        if (!m_world.has_resource<Events<E>>()) {
            m_world.add_resource(Events<E>());
        }
        auto event_type = type_id<Events<E>>();
        if (m_events.contains(event_type)) {
            return *this;
        }
        m_events.insert(event_type);
        add_systems(Last, event_update_system<E>);
        return *this;
    }

    App& add_systems(
        ScheduleId schedule,
        std::convertible_to<SystemConfigs> auto&&... systems
    ) {
        m_world.add_systems(
            schedule,
            std::forward<decltype(systems)>(systems)...
        );
        return *this;
    }

    template<typename F>
        requires IntoSystem<std::decay_t<F>>
    RegisteredSystemId register_system(F&& system) {
        return m_world.register_system(std::forward<F>(system));
    }

    App& configure_sets(
        ScheduleId schedule,
        std::convertible_to<SystemSetConfigs> auto&&... config
    ) {
        m_world.configure_sets(
            schedule,
            std::forward<decltype(config)>(config)...
        );
        return *this;
    }

    // TODO: Remove this and use init_resource instead
    template<typename R>
    App& add_resource() {
        m_world.add_resource(R {});
        return *this;
    }

    template<FromWorld R>
    App& init_resource() {
        m_world.init_resource<R>();
        return *this;
    }

    template<typename T>
    App& init_state(T&& state) {
        m_world.init_state(std::forward<T>(state));
        return *this;
    }

    template<typename R>
    App& add_resource(R&& resource) {
        m_world.add_resource(std::forward<R>(resource));
        return *this;
    }

    template<typename T, typename U>
    App& add_resource_as(U&& val) {
        m_world.add_resource_as<T>(std::forward<U>(val));
        return *this;
    }

    template<typename R>
    bool has_resource() const {
        return m_world.has_resource<R>();
    }

    template<std::derived_from<Plugin> P>
    App& add_plugin() {
        return add_boxed_plugin(
            type_id<P>(),
            type_name<P>(),
            std::make_unique<P>()
        );
    }

    template<typename P>
        requires std::derived_from<std::remove_cvref_t<P>, Plugin>
    App& add_plugin(P&& plugin) {
        using PluginT = std::remove_cvref_t<P>;
        return add_boxed_plugin(
            type_id<PluginT>(),
            type_name<PluginT>(),
            std::make_unique<PluginT>(std::forward<P>(plugin))
        );
    }

    App& add_plugin(std::string_view name);
    App& add_plugin(const PluginId& id);

    App& add_plugins(PluginGroupBuilder builder);

    template<typename G>
        requires std::derived_from<std::remove_cvref_t<G>, PluginGroup>
    App& add_plugins(G&& group) {
        return add_plugins(std::forward<G>(group).build());
    }

    template<typename... Plugins>
        requires(
            sizeof...(Plugins) > 0 &&
            (std::derived_from<std::remove_cvref_t<Plugins>, Plugin> && ...)
        )
    App& add_plugins(Plugins&&... plugins) {
        (add_plugin(std::forward<Plugins>(plugins)), ...);
        return *this;
    }

    template<std::derived_from<Plugin> P>
    bool has_plugin() const {
        return m_plugin_indices.contains(type_id<P>());
    }

    template<typename R>
    R& resource() {
        return m_world.resource<R>();
    }

    template<typename Label>
    [[nodiscard]] bool has_sub_app() const {
        const auto label = type_id<Label>();
        return std::ranges::any_of(m_sub_apps, [label](const auto& entry) {
            return entry.label == label;
        });
    }

    template<typename Label>
    App& insert_sub_app(SubApp sub_app) {
        return insert_sub_app<Label>(
            std::make_unique<InlineSubAppRunner>(std::move(sub_app))
        );
    }

    template<typename Label>
    App& insert_sub_app(std::unique_ptr<SubAppRunner> runner) {
        const auto label = type_id<Label>();
        if (has_sub_app<Label>()) {
            fatal("SubApp {} has already been inserted", type_name<Label>());
        }
        if (!runner) {
            fatal(
                "Cannot insert a null SubApp runner for {}",
                type_name<Label>()
            );
        }
        runner->set_worker_threads(m_world.worker_threads());
        m_sub_apps.push_back(
            LabeledSubApp {
                .label = label,
                .runner = std::move(runner),
                .source_selector = {},
            }
        );
        return *this;
    }

    // The selector is evaluated before each SubApp startup/update boundary.
    template<typename Label, typename Selector>
        requires std::invocable<Selector&, World&> &&
                 std::convertible_to<
                     std::invoke_result_t<Selector&, World&>,
                     SubAppSource>
    App& set_sub_app_source(Selector&& selector) {
        const auto label = type_id<Label>();
        for (auto& entry : m_sub_apps) {
            if (entry.label == label) {
                entry.source_selector = std::forward<Selector>(selector);
                return *this;
            }
        }
        fatal("SubApp {} not found", type_name<Label>());
    }

    template<typename Label>
    SubApp& sub_app() {
        const auto label = type_id<Label>();
        for (auto& entry : m_sub_apps) {
            if (entry.label == label) {
                return entry.runner->sub_app();
            }
        }
        fatal("SubApp {} not found", type_name<Label>());
    }

    template<typename Label>
    const SubApp& sub_app() const {
        const auto label = type_id<Label>();
        for (const auto& entry : m_sub_apps) {
            if (entry.label == label) {
                return entry.runner->sub_app();
            }
        }
        fatal("SubApp {} not found", type_name<Label>());
    }

    template<typename Label>
    SubAppRunner& sub_app_runner() {
        const auto label = type_id<Label>();
        for (auto& entry : m_sub_apps) {
            if (entry.label == label) {
                return *entry.runner;
            }
        }
        fatal("SubApp {} not found", type_name<Label>());
    }

    template<typename Label>
    const SubAppRunner& sub_app_runner() const {
        const auto label = type_id<Label>();
        for (const auto& entry : m_sub_apps) {
            if (entry.label == label) {
                return *entry.runner;
            }
        }
        fatal("SubApp {} not found", type_name<Label>());
    }

    template<typename Label>
    App& synchronize_sub_app() {
        sub_app_runner<Label>().synchronize();
        return *this;
    }

    World& world() { return m_world; }

    const World& world() const { return m_world; }

    [[nodiscard]] AppLifecycle lifecycle() const { return m_lifecycle; }

    App& set_worker_threads(std::size_t thread_count) {
        m_world.set_worker_threads(thread_count);
        for (auto& entry : m_sub_apps) {
            entry.runner->set_worker_threads(thread_count);
        }
        return *this;
    }

    void run_schedule(ScheduleId schedule) { m_world.run_schedule(schedule); }

    void finish();
    void startup();
    void update();
    void render();
    void shutdown() noexcept;
    void run();

  private:
    SubAppSource resolve_sub_app_source(LabeledSubApp& entry);

    AppLifecycle m_lifecycle {AppLifecycle::Building};
};

} // namespace fei
