#include "ecs/dynamic/events.hpp"

#include "ecs/world.hpp"

#include <utility>

namespace ets {

void DynamicEvents::send(TypeId type, Val event) {
    auto& channel = m_channels[type];
    channel.current.events.push_back(std::move(event));
    ++channel.event_count;
}

void DynamicEvents::update() {
    for (auto& [_, channel] : m_channels) {
        std::swap(channel.previous, channel.current);
        channel.current.events.clear();
        channel.current.start_event_count = channel.event_count;
    }
}

const DynamicEvents::Channel* DynamicEvents::channel(TypeId type) const {
    const auto found = m_channels.find(type);
    return found != m_channels.end() ? &found->second : nullptr;
}

DynamicEvents::Channel* DynamicEvents::channel(TypeId type) {
    const auto found = m_channels.find(type);
    return found != m_channels.end() ? &found->second : nullptr;
}

std::size_t DynamicEvents::oldest_event_count(TypeId type) const {
    const auto* found = channel(type);
    return found != nullptr ? found->previous.start_event_count : 0;
}

Optional<Ref>
DynamicEvents::get(TypeId type, std::size_t event_id, bool read_only) {
    auto* found = channel(type);
    if (found == nullptr || event_id < found->previous.start_event_count ||
        event_id >= found->event_count) {
        return nullopt;
    }
    auto& sequence = event_id < found->current.start_event_count ?
                         found->previous :
                         found->current;
    const auto index = event_id - sequence.start_event_count;
    if (index >= sequence.events.size()) {
        return nullopt;
    }
    if (read_only) {
        return static_cast<const Val&>(sequence.events[index]).ref();
    }
    return sequence.events[index].ref();
}

void DynamicEvents::set_channel(TypeId type, Channel channel) {
    m_channels[type] = std::move(channel);
}

DynamicEventParam::DynamicEventParam(
    std::string name,
    TypeId event_type,
    DynamicEventParamKind kind,
    bool optional
) :
    m_event_type(event_type), m_kind(kind), m_optional(optional),
    name(std::move(name)) {}

SystemAccess DynamicEventParam::access() const {
    SystemAccess result;
    if (m_kind == DynamicEventParamKind::ReaderRO) {
        result.read_resources.insert(type_id<DynamicEvents>());
    } else {
        result.write_resources.insert(type_id<DynamicEvents>());
    }
    return result;
}

Result<Ref, DynamicSystemError>
DynamicEventParam::prepare(World& world, SystemTicks) {
    if (!world.has_resource<DynamicEvents>()) {
        if (m_optional && m_kind == DynamicEventParamKind::ReaderRO) {
            m_events = nullptr;
            return Ref {};
        }
        world.add_resource(DynamicEvents {});
    }
    m_events = &world.resource<DynamicEvents>();
    if (m_optional && m_events->channel(m_event_type) == nullptr) {
        return Ref {};
    }
    if (m_kind != DynamicEventParamKind::Writer && !m_initialized) {
        m_cursor = m_events->oldest_event_count(m_event_type);
        m_initialized = true;
    }
    return Ref(*this);
}

Status<DynamicSystemError> DynamicEventParam::send(Val event) {
    if (m_kind != DynamicEventParamKind::Writer || m_events == nullptr) {
        return failure(DynamicSystemError {"EventWriter is not active"});
    }
    if (event.type_id() != m_event_type) {
        return failure(
            DynamicSystemError {"EventWriter payload type mismatch"}
        );
    }
    m_events->send(m_event_type, std::move(event));
    return {};
}

Optional<Ref> DynamicEventParam::next() {
    if (m_kind == DynamicEventParamKind::Writer || m_events == nullptr) {
        return nullopt;
    }
    const auto oldest = m_events->oldest_event_count(m_event_type);
    if (m_cursor < oldest) {
        m_cursor = oldest;
    }
    auto event = m_events->get(
        m_event_type,
        m_cursor,
        m_kind == DynamicEventParamKind::ReaderRO
    );
    if (event) {
        ++m_cursor;
    }
    return event;
}

void DynamicEventParam::reset() {
    m_cursor =
        m_events != nullptr ? m_events->oldest_event_count(m_event_type) : 0;
    m_initialized = true;
}

Result<SystemParamRuntimeState, RuntimeStateError>
DynamicEventParam::capture_runtime_state() const {
    if (m_kind == DynamicEventParamKind::Writer) {
        return DynamicSystemParam::capture_runtime_state();
    }
    return SystemParamRuntimeState {
        .kind = SystemParamRuntimeStateKind::Counter,
        .value = m_cursor,
        .parameter_type = runtime_state_type(),
    };
}

Status<RuntimeStateError> DynamicEventParam::validate_runtime_state(
    const SystemParamRuntimeState& state
) const {
    const auto expected = m_kind == DynamicEventParamKind::Writer ?
                              SystemParamRuntimeStateKind::Stateless :
                              SystemParamRuntimeStateKind::Counter;
    if (state.kind != expected ||
        state.parameter_type != runtime_state_type()) {
        return failure(
            RuntimeStateError {
                .message = "Dynamic event parameter runtime state type changed",
            }
        );
    }
    return {};
}

std::uint64_t DynamicEventParam::runtime_state_type() const {
    auto result = DynamicSystemParam::runtime_state_type();
    result ^= static_cast<std::uint64_t>(m_event_type.id()) << 1U;
    result ^= static_cast<std::uint64_t>(m_kind) << 48U;
    result ^= static_cast<std::uint64_t>(m_optional) << 56U;
    return result;
}

Status<RuntimeStateError>
DynamicEventParam::restore_runtime_state(const SystemParamRuntimeState& state) {
    auto valid = validate_runtime_state(state);
    if (!valid) {
        return valid;
    }
    if (m_kind != DynamicEventParamKind::Writer) {
        m_cursor = static_cast<std::size_t>(state.value);
        m_initialized = true;
    }
    return {};
}

} // namespace ets
