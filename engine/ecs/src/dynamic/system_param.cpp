#include "ecs/dynamic/system_param.hpp"

#include "ecs/world.hpp"

namespace fei {

Result<Ref, DynamicSystemError> DynamicSystemParam::prepare(World& world) {
    return prepare(
        world,
        SystemTicks {
            .last_run = 0,
            .this_run = world.increment_change_tick(),
        }
    );
}

SystemAccess
dynamic_system_access_for_params(const DynamicSystemParams& params) {
    SystemAccess result;
    for (const auto& param : params) {
        if (!param) {
            result.world_exclusive = true;
            continue;
        }
        result.merge(param->access());
    }
    return result;
}

} // namespace fei
