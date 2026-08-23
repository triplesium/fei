#include "ecs/dynamic/removed_components.hpp"

#include "ecs/world.hpp"

#include <utility>

namespace ets {

DynamicRemovedComponents::DynamicRemovedComponents(
    std::string name,
    TypeId component
) : m_component(component), name(std::move(name)) {}

Result<Ref, DynamicSystemError>
DynamicRemovedComponents::prepare(World& world, SystemTicks) {
    m_events = world.removed_components(m_component);
    if (!m_initialized) {
        m_cursor = m_events != nullptr ? m_events->oldest_event_count() : 0;
        m_initialized = true;
    }
    return Ref(*this);
}

Optional<Entity> DynamicRemovedComponents::next() {
    if (m_events == nullptr) {
        return nullopt;
    }
    if (m_cursor < m_events->oldest_event_count()) {
        m_cursor = m_events->oldest_event_count();
    }
    auto entity = m_events->get(m_cursor);
    if (entity) {
        ++m_cursor;
    }
    return entity;
}

void DynamicRemovedComponents::clear() {
    while (next()) {}
}

Result<SystemParamRuntimeState, RuntimeStateError>
DynamicRemovedComponents::capture_runtime_state() const {
    return SystemParamRuntimeState {
        .kind = SystemParamRuntimeStateKind::Counter,
        .value = m_cursor,
        .parameter_type = runtime_state_type(),
    };
}

Status<RuntimeStateError> DynamicRemovedComponents::validate_runtime_state(
    const SystemParamRuntimeState& state
) const {
    if (state.kind != SystemParamRuntimeStateKind::Counter ||
        state.parameter_type != runtime_state_type()) {
        return failure(
            RuntimeStateError {
                .message = "Expected RemovedComponents counter runtime state",
            }
        );
    }
    return {};
}

std::uint64_t DynamicRemovedComponents::runtime_state_type() const {
    auto result = DynamicSystemParam::runtime_state_type();
    return result ^ (static_cast<std::uint64_t>(m_component.id()) << 1U);
}

Status<RuntimeStateError> DynamicRemovedComponents::restore_runtime_state(
    const SystemParamRuntimeState& state
) {
    auto valid = validate_runtime_state(state);
    if (!valid) {
        return valid;
    }
    m_cursor = static_cast<std::size_t>(state.value);
    m_initialized = true;
    return {};
}

} // namespace ets
