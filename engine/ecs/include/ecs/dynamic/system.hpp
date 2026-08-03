#pragma once

#include "base/result.hpp"
#include "ecs/dynamic/system_param.hpp"
#include "ecs/system.hpp"
#include "refl/ref.hpp"

#include <memory>
#include <string>
#include <vector>

namespace fei {

class DynamicSystemExecutor {
  public:
    virtual ~DynamicSystemExecutor() = default;
    virtual Status<DynamicSystemError>
    execute(const std::vector<Ref>& args) = 0;
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
};

} // namespace fei
