#pragma once

#include "base/result.hpp"
#include "ecs/change_detection.hpp"
#include "ecs/runtime_state.hpp"
#include "ecs/system_access.hpp"
#include "refl/ref.hpp"

#include <memory>
#include <string>
#include <vector>

namespace ets {

class World;

struct DynamicSystemError {
    std::string message;
};

class DynamicSystemParam {
  public:
    virtual ~DynamicSystemParam() = default;

    virtual SystemAccess access() const = 0;
    Result<Ref, DynamicSystemError> prepare(World& world);
    virtual Result<Ref, DynamicSystemError>
    prepare(World& world, SystemTicks system_ticks) = 0;
    virtual void finish() {}
    virtual Result<SystemParamRuntimeState, RuntimeStateError>
    capture_runtime_state() const;
    virtual Status<RuntimeStateError>
    validate_runtime_state(const SystemParamRuntimeState& state) const;
    virtual Status<RuntimeStateError>
    restore_runtime_state(const SystemParamRuntimeState& state);
    virtual std::uint64_t runtime_state_type() const;
};

using DynamicSystemParamPtr = std::unique_ptr<DynamicSystemParam>;
using DynamicSystemParams = std::vector<DynamicSystemParamPtr>;

SystemAccess
dynamic_system_access_for_params(const DynamicSystemParams& params);

} // namespace ets
