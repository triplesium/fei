#include "ecs/system.hpp"

#include "ecs/world.hpp"

namespace fei {

void System::run(World& world) {
    SystemTicks system_ticks {
        .last_run = m_last_run,
        .this_run = world.increment_change_tick(),
    };
    execute(world, system_ticks);
    m_last_run = system_ticks.this_run;
}

bool Condition::run(World& world) {
    SystemTicks system_ticks {
        .last_run = m_last_run,
        .this_run = world.increment_change_tick(),
    };
    bool result = evaluate(world, system_ticks);
    m_last_run = system_ticks.this_run;
    return result;
}

} // namespace fei
