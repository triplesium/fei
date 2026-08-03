#include "ecs/dynamic/world.hpp"

#include "ecs/world.hpp"

#include <utility>

namespace fei {

DynamicWorld::DynamicWorld(std::string name) : m_name(std::move(name)) {}

SystemAccess DynamicWorld::access() const {
    SystemAccess result;
    result.world_exclusive = true;
    return result;
}

Result<Ref, DynamicSystemError>
DynamicWorld::prepare(World& world, SystemTicks system_ticks) {
    m_world = &world;
    m_system_ticks = system_ticks;
    m_commands.reset();
    m_structural_version = 0;
    ++m_generation;
    m_active = true;
    return Ref(*this);
}

void DynamicWorld::finish() {
    m_active = false;
}

Commands* DynamicWorld::commands() {
    if (!m_active || !m_world || !m_world->has_resource<CommandsQueue>()) {
        return nullptr;
    }
    if (!m_commands) {
        m_commands.emplace(m_world->resource<CommandsQueue>(), *m_world);
    }
    return &*m_commands;
}

} // namespace fei
