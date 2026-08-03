#pragma once

#include "ecs/change_detection.hpp"
#include "ecs/commands.hpp"
#include "ecs/dynamic/system_param.hpp"

#include <cstddef>
#include <optional>
#include <string>

namespace fei {

class DynamicWorld final : public DynamicSystemParam {
  private:
    std::string m_name;
    World* m_world {nullptr};
    SystemTicks m_system_ticks;
    std::optional<Commands> m_commands;
    std::size_t m_structural_version {0};
    std::size_t m_generation {0};
    bool m_active {false};

  public:
    using DynamicSystemParam::prepare;

    explicit DynamicWorld(std::string name);

    SystemAccess access() const override;
    Result<Ref, DynamicSystemError>
    prepare(World& world, SystemTicks system_ticks) override;
    void finish() override;

    bool active() const { return m_active; }
    World& world() const { return *m_world; }
    SystemTicks system_ticks() const { return m_system_ticks; }
    std::size_t structural_version() const { return m_structural_version; }
    std::size_t generation() const { return m_generation; }

    void mark_structural_change() { ++m_structural_version; }
    Commands* commands();
};

} // namespace fei
