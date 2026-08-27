#pragma once

#include "base/optional.hpp"

#include <cstddef>

namespace ets {

struct SystemExecutionLane {
    std::size_t index {0};
    std::size_t count {1};
    bool caller {true};

    bool operator==(const SystemExecutionLane&) const = default;
};

[[nodiscard]] Optional<SystemExecutionLane>
current_system_execution_lane() noexcept;

namespace detail {

class SystemExecutionLaneScope {
  private:
    Optional<SystemExecutionLane> m_previous;

  public:
    explicit SystemExecutionLaneScope(SystemExecutionLane lane) noexcept;
    ~SystemExecutionLaneScope();

    SystemExecutionLaneScope(const SystemExecutionLaneScope&) = delete;
    SystemExecutionLaneScope&
    operator=(const SystemExecutionLaneScope&) = delete;
};

} // namespace detail
} // namespace ets
