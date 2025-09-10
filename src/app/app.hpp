#pragma once
#include "app/plugin.hpp"
#include "ecs/commands.hpp"
#include "ecs/event.hpp"
#include "ecs/world.hpp"
#include "refl/type.hpp"
#include <cstdint>
#include <memory>
#include <unordered_map>
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

    RenderFirst,
    RenderStart,
    RenderUpdate,
    RenderEnd,
    RenderLast
};

struct AppStates {
    bool should_stop {false};
};

class App {
  private:
    std::unordered_map<TypeId, std::unique_ptr<Plugin>> m_plugins;

  public:
    App() {
        add_resource<AppStates>();
        add_resource<EventsMap>();
        add_resource<CommandsQueue>();
    }

    template<typename E>
    App& add_event() {
        m_world.resource<EventsMap>().add_event(type_id<E>());
        return *this;
    }

    template<typename F>
    App& add_system(uint32_t schedule, F system) {
        m_world.add_system(schedule, system);
        return *this;
    }

    template<typename R>
    App& add_resource() {
        m_world.add_resource(R {});
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
        m_plugins[type_id<P>()] = std::make_unique<P>();
        m_plugins[type_id<P>()]->setup(*this);
        return *this;
    }

    template<typename P>
    App& add_plugin(P&& plugin) {
        m_plugins[type_id<P>()] = std::make_unique<P>(std::forward<P>(plugin));
        m_plugins[type_id<P>()]->setup(*this);
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
