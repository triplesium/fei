#include "ecs/system.hpp"

#include "ecs/world.hpp"

namespace fei {

void SystemScheduler::run_systems(ScheduleId schedule, World& world) {
    auto it = m_systems.find(schedule);
    if (it != m_systems.end()) {
        for (auto& system : it->second) {
            system->run(world);
        }
    }
}

} // namespace fei
