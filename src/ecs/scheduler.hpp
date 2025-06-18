#include "ecs/system.hpp"

#include <memory>
#include <unordered_map>

namespace fei {

using ScheduleId = std::size_t;

class SystemScheduler {
  public:
    template<typename Func>
    void add_system(ScheduleId schedule, Func func) {
        m_systems[schedule] =
            std::make_unique<FunctionSystem<Func>>(std::move(func));
    }

    void run_systems(World& world);

  private:
    std::unordered_map<ScheduleId, std::unique_ptr<System>> m_systems;
};

} // namespace fei
