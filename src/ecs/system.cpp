#include "ecs/system.hpp"

#include "ecs/world.hpp"

namespace fei {

void SystemScheduler::run_systems(World& world) {
    for (auto& [id, system] : m_systems) {
        system->run(world);
    }
}

} // namespace fei
