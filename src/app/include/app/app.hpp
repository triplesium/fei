#pragma once
#include "app/plugin_group.hpp"
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
#include <string_view>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <vector>

namespace fei {

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

struct FEI_REFLECT AppStates {
    bool should_stop {false};
};

template<typename E>
void event_update_system(ResRW<Events<E>> events) {
    events->update();
}

class App {
  private:
    friend class PluginGroupBuilder;

    std::vector<std::unique_ptr<Plugin>> m_plugins;
    std::unordered_set<TypeId> m_plugin_types;
    std::unordered_set<TypeId> m_events;

    App& add_boxed_plugin(
        TypeId plugin_type,
        std::string_view plugin_name,
        std::unique_ptr<Plugin> plugin
    ) {
        if (!plugin) {
            fatal("Cannot add null plugin {}", plugin_name);
        }
        if (m_plugin_types.contains(plugin_type)) {
            fatal("Plugin {} has already been added", plugin_name);
        }

        auto* plugin_ptr = plugin.get();
        m_plugin_types.insert(plugin_type);
        m_plugins.emplace_back(std::move(plugin));
        plugin_ptr->setup(*this);
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
        return m_plugin_types.contains(type_id<P>());
    }

    template<typename R>
    R& resource() {
        return m_world.resource<R>();
    }

    World& world() { return m_world; }

    App& set_worker_threads(std::size_t thread_count) {
        m_world.set_worker_threads(thread_count);
        return *this;
    }

    void run_schedule(ScheduleId schedule) { m_world.run_schedule(schedule); }

    void run();

  private:
    World m_world;
};

} // namespace fei
