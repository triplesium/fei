#pragma once
#include "ecs/system.hpp"

#include <memory>
#include <unordered_map>
#include <vector>

namespace fei {

using ScheduleId = std::size_t;

class SystemScheduler {
  public:
    SystemScheduler() = default;

    // Delete copy constructor and copy assignment operator
    SystemScheduler(const SystemScheduler&) = delete;
    SystemScheduler& operator=(const SystemScheduler&) = delete;

    // Default move constructor and move assignment operator
    SystemScheduler(SystemScheduler&&) noexcept = default;
    SystemScheduler& operator=(SystemScheduler&&) noexcept = default;

    template<typename Func>
    void add_system(ScheduleId schedule, Func func) {
        m_systems[schedule].emplace_back(
            std::make_unique<FunctionSystem<Func>>(std::move(func))
        );
    }

    void run_systems(ScheduleId schedule, World& world);

  private:
    std::unordered_map<ScheduleId, std::vector<std::unique_ptr<System>>>
        m_systems;
};

} // namespace fei
