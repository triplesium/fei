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
    explicit DynamicCommandsParam(std::string name);

    SystemAccess access() const override;
    Result<Ref, DynamicSystemError> prepare(World& world) override;
};

} // namespace fei
