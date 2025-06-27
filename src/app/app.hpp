#pragma once

#include "ecs/commands.hpp"
#include "ecs/event.hpp"
#include "ecs/world.hpp"
#include "refl/type.hpp"
#include <cstdint>
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
  public:
    App() {
        add_resource<AppStates>();
        add_resource<EventsMap>();
        add_resource<CommandsQueue>();
    }

    template<class E>
    App& add_event() {
        m_world.get_resource<EventsMap>().add_event(type_id<E>());
        return *this;
    }

    template<class F>
    App& add_system(uint32_t schedule, F system) {
        m_world.add_system(schedule, system);
        return *this;
    }

    template<class R>
    App& add_resource() {
        m_world.add_resource(R {});
        return *this;
    }

    template<class R>
    App& add_resource(R&& resource) {
        m_world.add_resource(std::forward<R>(resource));
        return *this;
    }

    template<class P>
    App& add_plugin() {
        P plugin;
        plugin.setup(*this);
        return *this;
    }

    template<class R>
    R& get_resource() {
        return m_world.get_resource<R>();
    }

    void run_schedule(uint32_t schedule) { m_world.run_schedule(schedule); }

    void run();

  private:
    World m_world;
};

} // namespace fei
