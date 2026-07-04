#include "ecs/dynamic/system_param.hpp"

namespace fei {

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
