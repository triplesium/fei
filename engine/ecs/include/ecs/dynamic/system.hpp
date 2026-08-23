#pragma once

#include "base/result.hpp"
#include "ecs/dynamic/system_param.hpp"
#include "ecs/system.hpp"
#include "refl/ref.hpp"

#include <memory>
#include <string>
#include <vector>

namespace ets {

class DynamicSystemExecutor {
  public:
    virtual ~DynamicSystemExecutor() = default;
    virtual Status<DynamicSystemError>
    execute(const std::vector<Ref>& args) = 0;

    virtual bool checkpoint_safe_stateless() const { return false; }
    virtual Result<SystemExecutorRuntimeState, RuntimeStateError>
    capture_runtime_state() const;
    virtual Status<RuntimeStateError>
    validate_runtime_state(const SystemExecutorRuntimeState& state) const;
    virtual Status<RuntimeStateError>
    restore_runtime_state(const SystemExecutorRuntimeState& state);
};

class DynamicConditionExecutor {
  public:
    virtual ~DynamicConditionExecutor() = default;
    virtual Result<bool, DynamicSystemError>
    evaluate(const std::vector<Ref>& args) = 0;

    virtual bool checkpoint_safe_stateless() const { return false; }
    virtual Result<SystemExecutorRuntimeState, RuntimeStateError>
    capture_runtime_state() const;
    virtual Status<RuntimeStateError>
    validate_runtime_state(const SystemExecutorRuntimeState& state) const;
    virtual Status<RuntimeStateError>
    restore_runtime_state(const SystemExecutorRuntimeState& state);
};

class DynamicSystem : public System {
  private:
    std::string m_name;
    DynamicSystemParams m_params;
    std::unique_ptr<DynamicSystemExecutor> m_executor;
    SystemAccess m_access;

  public:
    DynamicSystem(
        std::string name,
        DynamicSystemParams params,
        std::unique_ptr<DynamicSystemExecutor> executor
    );

    const SystemAccess& access() const override { return m_access; }

  protected:
    void execute(World& world, SystemTicks system_ticks) override;
    Result<SystemExecutorRuntimeState, RuntimeStateError>
    capture_executor_runtime_state() const override;
    Status<RuntimeStateError> validate_executor_runtime_state(
        const SystemExecutorRuntimeState& state
    ) const override;
    Status<RuntimeStateError> restore_executor_runtime_state(
        const SystemExecutorRuntimeState& state
    ) override;
    Result<std::vector<SystemParamRuntimeState>, RuntimeStateError>
    capture_param_runtime_states() const override;
    Status<RuntimeStateError> validate_param_runtime_states(
        const std::vector<SystemParamRuntimeState>& states
    ) const override;
    Status<RuntimeStateError> restore_param_runtime_states(
        const std::vector<SystemParamRuntimeState>& states
    ) override;
};

class DynamicCondition : public Condition {
  private:
    std::string m_name;
    DynamicSystemParams m_params;
    std::unique_ptr<DynamicConditionExecutor> m_executor;
    SystemAccess m_access;

  public:
    DynamicCondition(
        std::string name,
        DynamicSystemParams params,
        std::unique_ptr<DynamicConditionExecutor> executor
    );

    const SystemAccess& access() const override { return m_access; }

  protected:
    bool evaluate(World& world, SystemTicks system_ticks) override;
    Result<SystemExecutorRuntimeState, RuntimeStateError>
    capture_executor_runtime_state() const override;
    Status<RuntimeStateError> validate_executor_runtime_state(
        const SystemExecutorRuntimeState& state
    ) const override;
    Status<RuntimeStateError> restore_executor_runtime_state(
        const SystemExecutorRuntimeState& state
    ) override;
    Result<std::vector<SystemParamRuntimeState>, RuntimeStateError>
    capture_param_runtime_states() const override;
    Status<RuntimeStateError> validate_param_runtime_states(
        const std::vector<SystemParamRuntimeState>& states
    ) const override;
    Status<RuntimeStateError> restore_param_runtime_states(
        const std::vector<SystemParamRuntimeState>& states
    ) override;
};

} // namespace ets
