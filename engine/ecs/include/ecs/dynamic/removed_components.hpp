#pragma once

#include "base/optional.hpp"
#include "ecs/dynamic/system_param.hpp"
#include "ecs/entity.hpp"
#include "ecs/removal_detection.hpp"
#include "refl/type.hpp"

#include <cstddef>
#include <string>

namespace fei {

class DynamicRemovedComponents final : public DynamicSystemParam {
  private:
    TypeId m_component;
    const RemovedComponentBuffer* m_events {nullptr};
    std::size_t m_cursor {};
    bool m_initialized {false};

  public:
    std::string name;

    DynamicRemovedComponents(std::string name, TypeId component);

    SystemAccess access() const override { return {}; }
    Result<Ref, DynamicSystemError>
    prepare(World& world, SystemTicks system_ticks) override;
    Optional<Entity> next();
    void clear();

    Result<SystemParamRuntimeState, RuntimeStateError>
    capture_runtime_state() const override;
    Status<RuntimeStateError>
    validate_runtime_state(const SystemParamRuntimeState& state) const override;
    Status<RuntimeStateError>
    restore_runtime_state(const SystemParamRuntimeState& state) override;
    std::uint64_t runtime_state_type() const override;
};

} // namespace fei
