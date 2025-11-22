#pragma once

#include "ecs/fwd.hpp"
#include "ecs/world.hpp"

#include <functional>
#include <queue>

namespace fei {

struct CommandsQueue {
    std::queue<std::function<void(World&)>> commands;

    void add_command(std::function<void(World&)> command) {
        commands.push(std::move(command));
    }
    void execute(World& world) {
        while (!commands.empty()) {
            auto command = std::move(commands.front());
            commands.pop();
            command(world);
        }
    }
    void clear() { std::queue<std::function<void(World&)>>().swap(commands); }
};

class EntityCommands {
  private:
    World& m_world;
    Entity m_entity;

  public:
    EntityCommands(World& world, Entity entity) :
        m_world(world), m_entity(entity) {}

    template<typename... Ts>
    EntityCommands& add(Ts&&... vals) {
        (m_world.resource<CommandsQueue>().add_command(
             [entity = this->m_entity,
              val = std::forward<Ts>(vals)](World& world) {
                 world.add_component(entity, val);
             }
         ),
         ...);
        return *this;
    }

    template<typename T>
    EntityCommands& remove() {
        m_world.remove_component(m_entity, type_id<T>());
        return *this;
    }

    template<typename T>
    bool has() const {
        return m_world.has_component(m_entity, type_id<T>());
    }

    Entity id() const { return m_entity; }
};

class Commands {
  private:
    CommandsQueue& m_commands_queue;
    World& m_world;

  public:
    Commands(CommandsQueue& queue, World& world) :
        m_commands_queue(queue), m_world(world) {}
    static Commands get_param(World& world) {
        return Commands(world.resource<CommandsQueue>(), world);
    }

    void add_command(std::function<void(World&)> command) {
        m_commands_queue.add_command(std::move(command));
    }

    EntityCommands entity(Entity entity) {
        return EntityCommands(m_world, entity);
    }

    EntityCommands spawn() { return EntityCommands(m_world, m_world.entity()); }

    World& world() { return m_world; }
};

} // namespace fei
