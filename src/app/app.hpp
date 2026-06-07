#pragma once
#include "app/plugin.hpp"
#include "ecs/commands.hpp"
#include "ecs/event.hpp"
#include "ecs/system_params.hpp"
#include "ecs/world.hpp"
#include "refl/reflect.hpp"
#include "refl/type.hpp"

#include <concepts>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <utility>

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
void event_update_system(Res<Events<E>> events) {
    events->update();
}

class App {
  private:
    std::unordered_map<TypeId, std::unique_ptr<Plugin>> m_plugins;
    std::unordered_set<TypeId> m_events;

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
        uint32_t schedule,
        std::convertible_to<SystemConfigs> auto&&... systems
    ) {
        m_world.add_systems(
            schedule,
            std::forward<decltype(systems)>(systems)...
        );
        return *this;
    }

    App& configure_sets(
        uint32_t schedule,
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

    template<typename P>
    App& add_plugin() {
        auto plugin_type = type_id<P>();
        if (m_plugins.contains(plugin_type)) {
            return *this;
        }
        auto plugin = std::make_unique<P>();
        auto* plugin_ptr = plugin.get();
        m_plugins.emplace(plugin_type, std::move(plugin));
        plugin_ptr->setup(*this);
        return *this;
    }

    template<std::derived_from<Plugin> P>
    App& add_plugin(P&& plugin) {
        auto plugin_type = type_id<P>();
        if (m_plugins.contains(plugin_type)) {
            return *this;
        }
        auto stored_plugin = std::make_unique<P>(std::forward<P>(plugin));
        auto* plugin_ptr = stored_plugin.get();
        m_plugins.emplace(plugin_type, std::move(stored_plugin));
        plugin_ptr->setup(*this);
        return *this;
    }

    App& add_plugins(std::derived_from<Plugin> auto&&... plugins) {
        (add_plugin(std::forward<decltype(plugins)>(plugins)), ...);
        return *this;
    }

    template<typename R>
    R& resource() {
        return m_world.resource<R>();
    }

    World& world() { return m_world; }

    void run_schedule(uint32_t schedule) { m_world.run_schedule(schedule); }

    void run();

  private:
    World m_world;
};

} // namespace fei
