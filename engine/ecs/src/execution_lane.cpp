#include "ecs/execution_lane.hpp"

#include <utility>

namespace ets {
namespace {

thread_local Optional<SystemExecutionLane> c_current_execution_lane;

} // namespace

Optional<SystemExecutionLane> current_system_execution_lane() noexcept {
    return c_current_execution_lane;
}

namespace detail {

SystemExecutionLaneScope::SystemExecutionLaneScope(
    SystemExecutionLane lane
) noexcept : m_previous(c_current_execution_lane) {
    c_current_execution_lane = lane;
}

SystemExecutionLaneScope::~SystemExecutionLaneScope() {
    c_current_execution_lane = std::move(m_previous);
}

} // namespace detail
} // namespace ets
