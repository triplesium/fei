#include "ecs/system.hpp"

#include "ecs/world.hpp"

#include <string_view>

namespace ets {

namespace {

RuntimeStateError
with_runtime_path(RuntimeStateError error, std::string_view prefix) {
    if (error.path.empty()) {
        error.path = prefix;
    } else {
        error.path = std::string(prefix) + "." + error.path;
    }
    return error;
}

Status<RuntimeStateError> validate_stateless_executor_state(
    const SystemExecutorRuntimeState& state,
    std::string_view kind
) {
    if (state.kind != SystemExecutorRuntimeStateKind::Stateless) {
        return failure(
            RuntimeStateError {
                .message = "Expected stateless " + std::string(kind) +
                           " executor state",
            }
        );
    }
    return {};
}

} // namespace

Result<SystemExecutorRuntimeState, RuntimeStateError>
System::capture_executor_runtime_state() const {
    return SystemExecutorRuntimeState::stateless();
}

Status<RuntimeStateError> System::validate_executor_runtime_state(
    const SystemExecutorRuntimeState& state
) const {
    return validate_stateless_executor_state(state, "system");
}

Status<RuntimeStateError> System::restore_executor_runtime_state(
    const SystemExecutorRuntimeState& state
) {
    return validate_executor_runtime_state(state);
}

Status<RuntimeStateError> System::validate_param_runtime_states(
    const std::vector<SystemParamRuntimeState>& states
) const {
    if (!states.empty()) {
        return failure(
            RuntimeStateError {
                .path = "params",
                .message = "System parameter count changed since checkpoint",
            }
        );
    }
    return {};
}

Status<RuntimeStateError> System::restore_param_runtime_states(
    const std::vector<SystemParamRuntimeState>& states
) {
    return validate_param_runtime_states(states);
}

Result<SystemRuntimeState, RuntimeStateError>
System::capture_runtime_state() const {
    if (m_pending_runtime_state) {
        return *m_pending_runtime_state;
    }
    auto executor = capture_executor_runtime_state();
    if (!executor) {
        return failure(
            with_runtime_path(std::move(executor.error()), "executor")
        );
    }
    auto params = capture_param_runtime_states();
    if (!params) {
        return failure(std::move(params.error()));
    }
    return SystemRuntimeState {
        .last_run = m_running ? m_current_run : m_last_run,
        .executor = std::move(*executor),
        .params = std::move(*params),
    };
}

Status<RuntimeStateError>
System::validate_runtime_state(const SystemRuntimeState& state) const {
    auto valid = validate_executor_runtime_state(state.executor);
    if (!valid) {
        return failure(with_runtime_path(std::move(valid.error()), "executor"));
    }
    valid = validate_param_runtime_states(state.params);
    if (!valid) {
        return valid;
    }
    return {};
}

Status<RuntimeStateError>
System::restore_runtime_state(const SystemRuntimeState& state) {
    auto valid = validate_runtime_state(state);
    if (!valid) {
        return valid;
    }
    if (m_running) {
        m_pending_runtime_state = state;
        return {};
    }
    auto restored = restore_executor_runtime_state(state.executor);
    if (!restored) {
        return failure(
            with_runtime_path(std::move(restored.error()), "executor")
        );
    }
    restored = restore_param_runtime_states(state.params);
    if (!restored) {
        return restored;
    }
    m_last_run = state.last_run;
    return {};
}

void System::run(World& world) {
    SystemTicks system_ticks {
        .last_run = m_last_run,
        .this_run = world.increment_change_tick(),
    };
    m_current_run = system_ticks.this_run;
    m_running = true;
    auto finish = [&](bool completed) {
        m_running = false;
        m_current_run = 0;
        if (!m_pending_runtime_state) {
            if (completed) {
                m_last_run = system_ticks.this_run;
            }
            return;
        }
        auto pending = std::move(*m_pending_runtime_state);
        m_pending_runtime_state.reset();
        auto restored = restore_executor_runtime_state(pending.executor);
        if (!restored) {
            fatal(
                "Failed to apply deferred system executor checkpoint state: "
                "{}",
                restored.error().message
            );
        }
        restored = restore_param_runtime_states(pending.params);
        if (!restored) {
            fatal(
                "Failed to apply deferred system checkpoint state: {}",
                restored.error().message
            );
        }
        m_last_run = pending.last_run;
    };
    try {
        execute(world, system_ticks);
    } catch (...) {
        finish(false);
        throw;
    }
    finish(true);
}

Result<SystemExecutorRuntimeState, RuntimeStateError>
Condition::capture_executor_runtime_state() const {
    return SystemExecutorRuntimeState::stateless();
}

Status<RuntimeStateError> Condition::validate_executor_runtime_state(
    const SystemExecutorRuntimeState& state
) const {
    return validate_stateless_executor_state(state, "condition");
}

Status<RuntimeStateError> Condition::restore_executor_runtime_state(
    const SystemExecutorRuntimeState& state
) {
    return validate_executor_runtime_state(state);
}

Status<RuntimeStateError> Condition::validate_param_runtime_states(
    const std::vector<SystemParamRuntimeState>& states
) const {
    if (!states.empty()) {
        return failure(
            RuntimeStateError {
                .path = "params",
                .message = "Condition parameter count changed since checkpoint",
            }
        );
    }
    return {};
}

Status<RuntimeStateError> Condition::restore_param_runtime_states(
    const std::vector<SystemParamRuntimeState>& states
) {
    return validate_param_runtime_states(states);
}

Result<SystemRuntimeState, RuntimeStateError>
Condition::capture_runtime_state() const {
    if (m_pending_runtime_state) {
        return *m_pending_runtime_state;
    }
    auto executor = capture_executor_runtime_state();
    if (!executor) {
        return failure(
            with_runtime_path(std::move(executor.error()), "executor")
        );
    }
    auto params = capture_param_runtime_states();
    if (!params) {
        return failure(std::move(params.error()));
    }
    return SystemRuntimeState {
        .last_run = m_running ? m_current_run : m_last_run,
        .executor = std::move(*executor),
        .params = std::move(*params),
    };
}

Status<RuntimeStateError>
Condition::validate_runtime_state(const SystemRuntimeState& state) const {
    auto valid = validate_executor_runtime_state(state.executor);
    if (!valid) {
        return failure(with_runtime_path(std::move(valid.error()), "executor"));
    }
    valid = validate_param_runtime_states(state.params);
    if (!valid) {
        return valid;
    }
    return {};
}

Status<RuntimeStateError>
Condition::restore_runtime_state(const SystemRuntimeState& state) {
    auto valid = validate_runtime_state(state);
    if (!valid) {
        return valid;
    }
    if (m_running) {
        m_pending_runtime_state = state;
        return {};
    }
    auto restored = restore_executor_runtime_state(state.executor);
    if (!restored) {
        return failure(
            with_runtime_path(std::move(restored.error()), "executor")
        );
    }
    restored = restore_param_runtime_states(state.params);
    if (!restored) {
        return restored;
    }
    m_last_run = state.last_run;
    return {};
}

bool Condition::run(World& world) {
    SystemTicks system_ticks {
        .last_run = m_last_run,
        .this_run = world.increment_change_tick(),
    };
    m_current_run = system_ticks.this_run;
    m_running = true;
    auto finish = [&](bool completed) {
        m_running = false;
        m_current_run = 0;
        if (!m_pending_runtime_state) {
            if (completed) {
                m_last_run = system_ticks.this_run;
            }
            return;
        }
        auto pending = std::move(*m_pending_runtime_state);
        m_pending_runtime_state.reset();
        auto restored = restore_executor_runtime_state(pending.executor);
        if (!restored) {
            fatal(
                "Failed to apply deferred condition executor checkpoint "
                "state: {}",
                restored.error().message
            );
        }
        restored = restore_param_runtime_states(pending.params);
        if (!restored) {
            fatal(
                "Failed to apply deferred condition checkpoint state: {}",
                restored.error().message
            );
        }
        m_last_run = pending.last_run;
    };
    try {
        const auto result = evaluate(world, system_ticks);
        finish(true);
        return result;
    } catch (...) {
        finish(false);
        throw;
    }
}

} // namespace ets
