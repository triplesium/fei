#pragma once

#include "ecs/fwd.hpp"
#include "ecs/system.hpp"
#include "ecs/world.hpp"

#include <functional>
#include <queue>
#include <utility>
#include <variant>
#include <vector>

namespace fei {

struct AddScheduleSystemCommand {
    ScheduleId schedule;
    SystemConfig config;
};

struct RemoveScheduleSystemCommand {
    SystemHandle handle;
};

struct ReplaceScheduleSystemCommand {
    SystemHandle handle;
    SystemConfig config;
};

// Schedule graph changes apply after the entire schedule rather than after an
// individual system batch. Keep them in a separate queue with explicit command
// types so their move-only SystemConfig values retain that timing.
using ScheduleCommand = std::variant<
    AddScheduleSystemCommand,
    RemoveScheduleSystemCommand,
    ReplaceScheduleSystemCommand>;

struct CommandsQueue {
    using BatchCommand = std::move_only_function<void(World&)>;

    std::queue<BatchCommand> after_batch_commands;
    std::vector<ScheduleCommand> after_schedule_commands;
    bool executing_after_batch {false};

    CommandsQueue() = default;
    CommandsQueue(const CommandsQueue&) = delete;
    CommandsQueue& operator=(const CommandsQueue&) = delete;
    CommandsQueue(CommandsQueue&&) noexcept = default;
    CommandsQueue& operator=(CommandsQueue&&) noexcept = default;

    void add_command(BatchCommand command) {
        after_batch_commands.push(std::move(command));
    }

    void add_schedule_command(ScheduleCommand command) {
        after_schedule_commands.push_back(std::move(command));
    }

    void execute_after_batch(World& world) {
        if (executing_after_batch) {
            return;
        }

        executing_after_batch = true;
        try {
            while (!after_batch_commands.empty()) {
                auto command = std::move(after_batch_commands.front());
                after_batch_commands.pop();
                command(world);
            }
        } catch (...) {
            executing_after_batch = false;
            throw;
        }
        executing_after_batch = false;
    }

    void execute_after_schedule(World& world) {
        std::vector<ScheduleCommand> commands;
        commands.swap(after_schedule_commands);
        for (auto& command : commands) {
            std::visit(
                [&world](auto& schedule_command) {
                    execute_schedule_command(world, schedule_command);
                },
                command
            );
        }
    }

    void execute(World& world) {
        execute_after_batch(world);
        execute_after_schedule(world);
    }

    void clear() {
        std::queue<BatchCommand>().swap(after_batch_commands);
        after_schedule_commands.clear();
        executing_after_batch = false;
    }

  private:
    static void
    execute_schedule_command(World& world, AddScheduleSystemCommand& command) {
        world.add_system(command.schedule, std::move(command.config));
    }

    static void execute_schedule_command(
        World& world,
        RemoveScheduleSystemCommand& command
    ) {
        world.remove_system(command.handle);
    }

    static void execute_schedule_command(
        World& world,
        ReplaceScheduleSystemCommand& command
    ) {
        world.replace_system(command.handle, std::move(command.config));
    }
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
        m_world.resource<CommandsQueue>().add_command(
            [entity = this->m_entity](World& world) {
                world.remove_component(entity, type_id<T>());
            }
        );
        return *this;
    }

    template<typename T>
    bool has() const {
        return m_world.has_component(m_entity, type_id<T>());
    }

    EntityCommands& set_parent(Entity parent) {
        m_world.resource<CommandsQueue>().add_command([entity = this->m_entity,
                                                       parent](World& world) {
            world.set_parent(entity, parent);
        });
        return *this;
    }

    EntityCommands& remove_parent() {
        m_world.resource<CommandsQueue>().add_command(
            [entity = this->m_entity](World& world) {
                world.remove_parent(entity);
            }
        );
        return *this;
    }

    void despawn() {
        m_world.resource<CommandsQueue>().add_command(
            [entity = this->m_entity](World& world) {
                world.despawn(entity);
            }
        );
    }

    void despawn_recursive() { despawn(); }

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

    void add_command(CommandsQueue::BatchCommand command) {
        m_commands_queue.add_command(std::move(command));
    }

    template<typename R>
    void add_resource(R&& resource) {
        m_commands_queue.add_command(
            [res = std::forward<R>(resource)](World& world) {
                world.add_resource(std::move(res));
            }
        );
    }

    SystemHandle add_system(ScheduleId schedule, SystemConfig config) {
        SystemHandle handle {.schedule = schedule, .id = config.id};
        m_commands_queue.add_schedule_command(
            AddScheduleSystemCommand {
                .schedule = schedule,
                .config = std::move(config)
            }
        );
        return handle;
    }

    void remove_system(SystemHandle handle) {
        m_commands_queue.add_schedule_command(
            RemoveScheduleSystemCommand {.handle = handle}
        );
    }

    void replace_system(SystemHandle handle, SystemConfig config) {
        m_commands_queue.add_schedule_command(
            ReplaceScheduleSystemCommand {
                .handle = handle,
                .config = std::move(config)
            }
        );
    }

    void run_system(RegisteredSystemId id) {
        m_commands_queue.add_command([id](World& world) {
            auto status = world.run_system(id);
            if (!status) {
                const auto* reason =
                    status.error() == RegisteredSystemError::NotFound ?
                        "not found" :
                        "already running";
                error(
                    "Failed to run registered system {}: {}",
                    id.value,
                    reason
                );
            }
        });
    }

    template<typename F>
        requires IntoSystem<std::decay_t<F>>
    void run_system_once(F&& system) {
        using Func = std::decay_t<F>;
        m_commands_queue.add_command(
            [system = Func(std::forward<F>(system))](World& world) mutable {
                world.run_system_once(std::move(system));
            }
        );
    }

    void unregister_system(RegisteredSystemId id) {
        m_commands_queue.add_command([id](World& world) {
            auto status = world.unregister_system(id);
            if (!status) {
                const auto* reason =
                    status.error() == RegisteredSystemError::NotFound ?
                        "not found" :
                        "already running";
                error(
                    "Failed to unregister registered system {}: {}",
                    id.value,
                    reason
                );
            }
        });
    }

    EntityCommands entity(Entity entity) {
        return EntityCommands(m_world, entity);
    }

    EntityCommands spawn() { return EntityCommands(m_world, m_world.entity()); }

    World& world() { return m_world; }
};
template<>
struct SystemParamTraits<Commands> : StatelessParamTraits<Commands> {};
static_assert(SystemParam<Commands>);

} // namespace fei
