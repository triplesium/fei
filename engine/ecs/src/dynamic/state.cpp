#include "ecs/dynamic/state.hpp"

#include "ecs/world.hpp"

#include <mutex>

namespace fei {

DynamicStateRegistry& DynamicStateRegistry::instance() {
    static DynamicStateRegistry registry;
    return registry;
}

void DynamicStateRegistry::add(DynamicStateOps ops) {
    std::unique_lock lock(m_mutex);
    m_states.insert_or_assign(ops.value_type, ops);
}

Optional<DynamicStateOps> DynamicStateRegistry::find(TypeId value_type) const {
    std::shared_lock lock(m_mutex);
    const auto found = m_states.find(value_type);
    if (found == m_states.end()) {
        return nullopt;
    }
    return found->second;
}

Result<DynamicStateOps, DynamicSystemError>
resolve_dynamic_state(TypeId value_type) {
    auto found = DynamicStateRegistry::instance().find(value_type);
    if (!found) {
        return failure(
            DynamicSystemError {
                "State type '" + type_name(value_type) +
                "' has not been initialized in a World"
            }
        );
    }
    return *found;
}

Ref DynamicStateRef::get() const {
    if (m_world == nullptr || m_ops.current == nullptr) {
        return {};
    }
    return m_ops.current(*m_world);
}

Status<DynamicSystemError> DynamicNextStateRef::set(Ref value) const {
    if (m_world == nullptr || m_ops.set_next == nullptr) {
        return failure(
            DynamicSystemError {"NextState proxy is not initialized"}
        );
    }
    return m_ops.set_next(*m_world, value);
}

void DynamicNextStateRef::clear() const {
    if (m_world != nullptr && m_ops.clear_next != nullptr) {
        m_ops.clear_next(*m_world);
    }
}

SystemAccess DynamicStateParam::access() const {
    SystemAccess result;
    result.read_resources.insert(m_ops.state_resource);
    return result;
}

Result<Ref, DynamicSystemError>
DynamicStateParam::prepare(World& world, SystemTicks system_ticks) {
    static_cast<void>(system_ticks);
    if (m_ops.initialized == nullptr || !m_ops.initialized(world)) {
        return failure(
            DynamicSystemError {
                "State type '" + type_name(m_ops.value_type) +
                "' is not initialized in this World"
            }
        );
    }
    m_ref = DynamicStateRef(world, m_ops);
    return Ref(static_cast<const DynamicStateRef&>(m_ref));
}

SystemAccess DynamicNextStateParam::access() const {
    SystemAccess result;
    result.write_resources.insert(m_ops.next_state_resource);
    return result;
}

Result<Ref, DynamicSystemError>
DynamicNextStateParam::prepare(World& world, SystemTicks system_ticks) {
    static_cast<void>(system_ticks);
    if (m_ops.initialized == nullptr || !m_ops.initialized(world)) {
        return failure(
            DynamicSystemError {
                "State type '" + type_name(m_ops.value_type) +
                "' is not initialized in this World"
            }
        );
    }
    m_ref = DynamicNextStateRef(world, m_ops);
    return Ref(m_ref);
}

} // namespace fei
