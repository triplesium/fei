#include "ecs/dynamic/system.hpp"

#include "base/log.hpp"
#include "ecs/world.hpp"

#include <string_view>
#include <utility>
#include <vector>

namespace fei {
namespace {

Result<std::vector<Ref>, DynamicSystemError> prepare_dynamic_params(
    std::string_view name,
    DynamicSystemParams& params,
    World& world,
    SystemTicks system_ticks,
    std::vector<DynamicSystemParam*>& prepared_params
) {
    std::vector<Ref> args;
    args.reserve(params.size());
    prepared_params.reserve(params.size());
    for (auto& param : params) {
        if (!param) {
            return failure(
                DynamicSystemError {
                    "Dynamic callable '" + std::string(name) +
                    "' has null param"
                }
            );
        }

        auto arg = param->prepare(world, system_ticks);
        if (!arg) {
            return failure(
                DynamicSystemError {
                    "Dynamic callable '" + std::string(name) +
                    "' failed to prepare param: " + arg.error().message
                }
            );
        }
        args.push_back(*arg);
        prepared_params.push_back(param.get());
    }
    return args;
}

void finish_dynamic_params(std::vector<DynamicSystemParam*>& params) {
    for (auto* param : params) {
        param->finish();
    }
    params.clear();
}

Result<std::vector<SystemParamRuntimeState>, RuntimeStateError>
capture_dynamic_param_states(const DynamicSystemParams& params) {
    std::vector<SystemParamRuntimeState> result;
    result.reserve(params.size());
    for (std::size_t index = 0; index < params.size(); ++index) {
        if (!params[index]) {
            return failure(
                RuntimeStateError {
                    .path = "params." + std::to_string(index),
                    .message = "Dynamic system parameter is null",
                }
            );
        }
        auto state = params[index]->capture_runtime_state();
        if (!state) {
            auto error = std::move(state.error());
            error.path = "params." + std::to_string(index) +
                         (error.path.empty() ? "" : "." + error.path);
            return failure(std::move(error));
        }
        result.push_back(*state);
    }
    return result;
}

Status<RuntimeStateError> validate_dynamic_param_states(
    const DynamicSystemParams& params,
    const std::vector<SystemParamRuntimeState>& states
) {
    if (states.size() != params.size()) {
        return failure(
            RuntimeStateError {
                .path = "params",
                .message = "Dynamic parameter count changed since checkpoint",
            }
        );
    }
    for (std::size_t index = 0; index < params.size(); ++index) {
        if (!params[index]) {
            return failure(
                RuntimeStateError {
                    .path = "params." + std::to_string(index),
                    .message = "Dynamic system parameter is null",
                }
            );
        }
        auto valid = params[index]->validate_runtime_state(states[index]);
        if (!valid) {
            auto error = std::move(valid.error());
            error.path = "params." + std::to_string(index) +
                         (error.path.empty() ? "" : "." + error.path);
            return failure(std::move(error));
        }
    }
    return {};
}

Status<RuntimeStateError> restore_dynamic_param_states(
    DynamicSystemParams& params,
    const std::vector<SystemParamRuntimeState>& states
) {
    auto valid = validate_dynamic_param_states(params, states);
    if (!valid) {
        return valid;
    }
    for (std::size_t index = 0; index < params.size(); ++index) {
        auto restored = params[index]->restore_runtime_state(states[index]);
        if (!restored) {
            auto error = std::move(restored.error());
            error.path = "params." + std::to_string(index) +
                         (error.path.empty() ? "" : "." + error.path);
            return failure(std::move(error));
        }
    }
    return {};
}

Result<SystemExecutorRuntimeState, RuntimeStateError>
capture_default_executor_state(bool checkpoint_safe, std::string_view kind) {
    if (!checkpoint_safe) {
        return failure(
            RuntimeStateError {
                .message = "Dynamic " + std::string(kind) +
                           " executor has no checkpoint adapter",
            }
        );
    }
    return SystemExecutorRuntimeState::stateless();
}

Status<RuntimeStateError> validate_default_executor_state(
    bool checkpoint_safe,
    std::string_view kind,
    const SystemExecutorRuntimeState& state
) {
    if (!checkpoint_safe) {
        return failure(
            RuntimeStateError {
                .message = "Dynamic " + std::string(kind) +
                           " executor has no checkpoint adapter",
            }
        );
    }
    if (state.kind != SystemExecutorRuntimeStateKind::Stateless) {
        return failure(
            RuntimeStateError {
                .message = "Expected stateless dynamic " + std::string(kind) +
                           " executor state",
            }
        );
    }
    return {};
}

} // namespace

Result<SystemExecutorRuntimeState, RuntimeStateError>
DynamicSystemExecutor::capture_runtime_state() const {
    return capture_default_executor_state(
        checkpoint_safe_stateless(),
        "system"
    );
}

Status<RuntimeStateError> DynamicSystemExecutor::validate_runtime_state(
    const SystemExecutorRuntimeState& state
) const {
    return validate_default_executor_state(
        checkpoint_safe_stateless(),
        "system",
        state
    );
}

Status<RuntimeStateError> DynamicSystemExecutor::restore_runtime_state(
    const SystemExecutorRuntimeState& state
) {
    return validate_runtime_state(state);
}

Result<SystemExecutorRuntimeState, RuntimeStateError>
DynamicConditionExecutor::capture_runtime_state() const {
    return capture_default_executor_state(
        checkpoint_safe_stateless(),
        "condition"
    );
}

Status<RuntimeStateError> DynamicConditionExecutor::validate_runtime_state(
    const SystemExecutorRuntimeState& state
) const {
    return validate_default_executor_state(
        checkpoint_safe_stateless(),
        "condition",
        state
    );
}

Status<RuntimeStateError> DynamicConditionExecutor::restore_runtime_state(
    const SystemExecutorRuntimeState& state
) {
    return validate_runtime_state(state);
}

DynamicSystem::DynamicSystem(
    std::string name,
    DynamicSystemParams params,
    std::unique_ptr<DynamicSystemExecutor> executor
) :
    m_name(std::move(name)), m_params(std::move(params)),
    m_executor(std::move(executor)),
    m_access(dynamic_system_access_for_params(m_params)) {}

void DynamicSystem::execute(World& world, SystemTicks system_ticks) {
    std::vector<DynamicSystemParam*> prepared_params;
    auto args = prepare_dynamic_params(
        m_name,
        m_params,
        world,
        system_ticks,
        prepared_params
    );
    if (!args) {
        error("{}", args.error().message);
        finish_dynamic_params(prepared_params);
        return;
    }

    if (!m_executor) {
        error("Dynamic system '{}' missing executor", m_name);
        finish_dynamic_params(prepared_params);
        return;
    }

    auto status = m_executor->execute(*args);
    finish_dynamic_params(prepared_params);
    if (!status) {
        error("Dynamic system '{}' failed: {}", m_name, status.error().message);
    }
}

Result<SystemExecutorRuntimeState, RuntimeStateError>
DynamicSystem::capture_executor_runtime_state() const {
    if (!m_executor) {
        return failure(
            RuntimeStateError {
                .message = "Dynamic system is missing its executor",
            }
        );
    }
    return m_executor->capture_runtime_state();
}

Status<RuntimeStateError> DynamicSystem::validate_executor_runtime_state(
    const SystemExecutorRuntimeState& state
) const {
    if (!m_executor) {
        return failure(
            RuntimeStateError {
                .message = "Dynamic system is missing its executor",
            }
        );
    }
    return m_executor->validate_runtime_state(state);
}

Status<RuntimeStateError> DynamicSystem::restore_executor_runtime_state(
    const SystemExecutorRuntimeState& state
) {
    if (!m_executor) {
        return failure(
            RuntimeStateError {
                .message = "Dynamic system is missing its executor",
            }
        );
    }
    return m_executor->restore_runtime_state(state);
}

Result<std::vector<SystemParamRuntimeState>, RuntimeStateError>
DynamicSystem::capture_param_runtime_states() const {
    return capture_dynamic_param_states(m_params);
}

Status<RuntimeStateError> DynamicSystem::validate_param_runtime_states(
    const std::vector<SystemParamRuntimeState>& states
) const {
    return validate_dynamic_param_states(m_params, states);
}

Status<RuntimeStateError> DynamicSystem::restore_param_runtime_states(
    const std::vector<SystemParamRuntimeState>& states
) {
    return restore_dynamic_param_states(m_params, states);
}

DynamicCondition::DynamicCondition(
    std::string name,
    DynamicSystemParams params,
    std::unique_ptr<DynamicConditionExecutor> executor
) :
    m_name(std::move(name)), m_params(std::move(params)),
    m_executor(std::move(executor)),
    m_access(dynamic_system_access_for_params(m_params)) {}

bool DynamicCondition::evaluate(World& world, SystemTicks system_ticks) {
    std::vector<DynamicSystemParam*> prepared_params;
    auto args = prepare_dynamic_params(
        m_name,
        m_params,
        world,
        system_ticks,
        prepared_params
    );
    if (!args) {
        error("{}", args.error().message);
        finish_dynamic_params(prepared_params);
        return false;
    }
    if (!m_executor) {
        error("Dynamic condition '{}' missing executor", m_name);
        finish_dynamic_params(prepared_params);
        return false;
    }

    auto result = m_executor->evaluate(*args);
    finish_dynamic_params(prepared_params);
    if (!result) {
        error(
            "Dynamic condition '{}' failed: {}",
            m_name,
            result.error().message
        );
        return false;
    }
    return *result;
}

Result<SystemExecutorRuntimeState, RuntimeStateError>
DynamicCondition::capture_executor_runtime_state() const {
    if (!m_executor) {
        return failure(
            RuntimeStateError {
                .message = "Dynamic condition is missing its executor",
            }
        );
    }
    return m_executor->capture_runtime_state();
}

Status<RuntimeStateError> DynamicCondition::validate_executor_runtime_state(
    const SystemExecutorRuntimeState& state
) const {
    if (!m_executor) {
        return failure(
            RuntimeStateError {
                .message = "Dynamic condition is missing its executor",
            }
        );
    }
    return m_executor->validate_runtime_state(state);
}

Status<RuntimeStateError> DynamicCondition::restore_executor_runtime_state(
    const SystemExecutorRuntimeState& state
) {
    if (!m_executor) {
        return failure(
            RuntimeStateError {
                .message = "Dynamic condition is missing its executor",
            }
        );
    }
    return m_executor->restore_runtime_state(state);
}

Result<std::vector<SystemParamRuntimeState>, RuntimeStateError>
DynamicCondition::capture_param_runtime_states() const {
    return capture_dynamic_param_states(m_params);
}

Status<RuntimeStateError> DynamicCondition::validate_param_runtime_states(
    const std::vector<SystemParamRuntimeState>& states
) const {
    return validate_dynamic_param_states(m_params, states);
}

Status<RuntimeStateError> DynamicCondition::restore_param_runtime_states(
    const std::vector<SystemParamRuntimeState>& states
) {
    return restore_dynamic_param_states(m_params, states);
}

} // namespace fei
