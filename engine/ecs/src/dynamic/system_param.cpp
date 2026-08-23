#include "ecs/dynamic/system_param.hpp"

#include "ecs/world.hpp"

#include <algorithm>
#include <typeinfo>

namespace ets {

Result<SystemParamRuntimeState, RuntimeStateError>
DynamicSystemParam::capture_runtime_state() const {
    return SystemParamRuntimeState {
        .kind = SystemParamRuntimeStateKind::Stateless,
        .parameter_type = runtime_state_type(),
    };
}

Status<RuntimeStateError> DynamicSystemParam::validate_runtime_state(
    const SystemParamRuntimeState& state
) const {
    if (state.kind != SystemParamRuntimeStateKind::Stateless ||
        state.parameter_type != runtime_state_type()) {
        return failure(
            RuntimeStateError {
                .message =
                    "Dynamic system parameter runtime state type changed",
            }
        );
    }
    return {};
}

std::uint64_t DynamicSystemParam::runtime_state_type() const {
    std::uint64_t result =
        static_cast<std::uint64_t>(typeid(*this).hash_code());
    const auto param_access = access();
    const auto add_types = [&](const auto& values, std::uint64_t salt) {
        std::vector<std::uint64_t> ids;
        ids.reserve(values.size());
        for (const auto type : values) {
            ids.push_back(type.id());
        }
        std::ranges::sort(ids);
        for (const auto id : ids) {
            result ^= id + salt + (result << 6U) + (result >> 2U);
        }
    };
    add_types(param_access.read_resources, 0x11U);
    add_types(param_access.write_resources, 0x22U);
    add_types(param_access.read_components, 0x33U);
    add_types(param_access.write_components, 0x44U);
    result ^= static_cast<std::uint64_t>(param_access.world_exclusive) << 1U;
    result ^= static_cast<std::uint64_t>(param_access.commands) << 2U;
    result ^= static_cast<std::uint64_t>(param_access.deferred_commands) << 3U;
    return result;
}

Status<RuntimeStateError> DynamicSystemParam::restore_runtime_state(
    const SystemParamRuntimeState& state
) {
    return validate_runtime_state(state);
}

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

} // namespace ets
