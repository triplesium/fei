#include "ecs/dynamic/commands.hpp"

#include "ecs/world.hpp"

#include <utility>

namespace fei {

DynamicCommandsParam::DynamicCommandsParam(std::string name) :
    m_name(std::move(name)) {}

SystemAccess DynamicCommandsParam::access() const {
    SystemAccess result;
    result.commands = true;
    result.write_resources.insert(type_id<CommandsQueue>());
    return result;
}

Result<Ref, DynamicSystemError>
DynamicCommandsParam::prepare(World& world, SystemTicks system_ticks) {
    (void)system_ticks;
    if (!world.has_resource<CommandsQueue>()) {
        return failure(
            DynamicSystemError {
                "missing resource '" + std::string(type_name<CommandsQueue>()) +
                "'"
            }
        );
    }

    m_commands.emplace(world.resource<CommandsQueue>(), world);
    return Ref(*m_commands);
}

} // namespace fei
