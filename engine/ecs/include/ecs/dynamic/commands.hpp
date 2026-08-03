#pragma once

#include "ecs/commands.hpp"
#include "ecs/dynamic/system_param.hpp"

#include <optional>
#include <string>

namespace fei {

class DynamicCommandsParam final : public DynamicSystemParam {
  private:
    std::string m_name;
    std::optional<Commands> m_commands;

  public:
    using DynamicSystemParam::prepare;

    explicit DynamicCommandsParam(std::string name);

    SystemAccess access() const override;
    Result<Ref, DynamicSystemError>
    prepare(World& world, SystemTicks system_ticks) override;
};

} // namespace fei
