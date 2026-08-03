#pragma once

#include "base/optional.hpp"
#include "ecs/commands.hpp"
#include "ecs/system_params.hpp"
#include "refl/type.hpp"

#include <concepts>
#include <cstdint>
#include <functional>
#include <type_traits>
#include <utility>

namespace fei {

inline constexpr ScheduleId StateTransitionSchedule =
    stable_type_hash("fei::StateTransitionSchedule");

template<typename T>
class State;

template<typename T>
class NextState;

template<typename T>
concept HashableStateValue = std::is_enum_v<std::remove_cvref_t<T>> ||
                             requires(const std::remove_cvref_t<T>& value) {
                                 {
                                     std::hash<std::remove_cvref_t<T>> {}(value)
                                 } -> std::convertible_to<std::size_t>;
                             };

template<typename T>
concept StateValue =
    std::equality_comparable<std::remove_cvref_t<T>> && HashableStateValue<T>;

namespace detail {

inline std::size_t hash_combine(std::size_t seed, std::size_t value) {
    return seed ^ (value + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2));
}

template<StateValue T>
std::size_t hash_state_value(const T& state) {
    using StateType = std::remove_cvref_t<T>;
    if constexpr (std::is_enum_v<StateType>) {
        using Underlying = std::underlying_type_t<StateType>;
        return std::hash<Underlying> {}(static_cast<Underlying>(state));
    } else {
        return std::hash<StateType> {}(state);
    }
}

template<StateValue T>
std::size_t hash_state_schedule_part(std::size_t seed, const T& state) {
    seed = hash_combine(seed, static_cast<std::size_t>(type_id<T>().id()));
    seed = hash_combine(seed, hash_state_value(state));
    return seed;
}

template<StateValue T>
ScheduleId
single_state_schedule_id(std::uint64_t schedule_kind, const T& state) {
    auto seed = static_cast<std::size_t>(schedule_kind);
    return hash_state_schedule_part(seed, state);
}

template<StateValue T>
ScheduleId transition_schedule_id(
    std::uint64_t schedule_kind,
    const T& from,
    const T& to
) {
    auto seed = static_cast<std::size_t>(schedule_kind);
    seed = hash_state_schedule_part(seed, from);
    seed = hash_state_schedule_part(seed, to);
    return seed;
}

} // namespace detail

template<StateValue T>
ScheduleId on_enter(const T& state) {
    return detail::single_state_schedule_id(
        stable_type_hash("fei::OnEnter"),
        state
    );
}

template<StateValue T>
ScheduleId on_exit(const T& state) {
    return detail::single_state_schedule_id(
        stable_type_hash("fei::OnExit"),
        state
    );
}

template<StateValue T>
ScheduleId on_transition(const T& from, const T& to) {
    return detail::transition_schedule_id(
        stable_type_hash("fei::OnTransition"),
        from,
        to
    );
}

template<StateValue T>
void apply_state_transition(
    WorldRef world,
    ResRW<State<T>> state,
    ResRW<NextState<T>> next_state
);

template<typename T>
class State {
  public:
    using ValueType = T;

    State()
        requires std::default_initializable<T>
        : m_state() {}

    explicit State(T state) : m_state(std::move(state)) {}

    const T& get() const { return m_state; }
    const T& operator*() const { return m_state; }
    const T* operator->() const { return &m_state; }

  private:
    T m_state;

    void set(T state) { m_state = std::move(state); }

    template<StateValue U>
    friend void apply_state_transition(
        WorldRef world,
        ResRW<State<U>> state,
        ResRW<NextState<U>> next_state
    );
};

template<typename T>
class NextState {
  public:
    using ValueType = T;

    NextState() = default;
    explicit NextState(T state) : m_state(std::move(state)) {}

    void set(T state) { m_state = std::move(state); }
    void reset() { m_state.reset(); }
    void clear() { reset(); }

    bool has_value() const { return m_state.has_value(); }
    explicit operator bool() const { return has_value(); }

    const Optional<T>& pending() const { return m_state; }

    Optional<T> take() {
        Optional<T> state = std::move(m_state);
        m_state.reset();
        return state;
    }

  private:
    Optional<T> m_state;
};

template<StateValue T>
void apply_state_transition(
    WorldRef world,
    ResRW<State<T>> state,
    ResRW<NextState<T>> next_state
) {
    auto pending = next_state->take();
    if (!pending) {
        return;
    }

    T old_state = state->get();
    T new_state = std::move(*pending);
    if (old_state == new_state) {
        return;
    }

    world->run_schedule(on_exit(old_state));
    state->set(std::move(new_state));
    world->run_schedule(on_transition(old_state, state->get()));
    world->run_schedule(on_enter(state->get()));
}

template<typename T>
auto in_state(T&& expected) {
    using StateValue = std::remove_cvref_t<T>;
    return [expected = StateValue(std::forward<T>(expected))](
               ResRO<State<StateValue>> state
           ) {
        return state->get() == expected;
    };
}

template<typename T>
State<std::remove_cvref_t<T>>& World::init_state(T&& state) {
    using StateType = std::remove_cvref_t<T>;
    static_assert(
        fei::StateValue<StateType>,
        "State values must be equality comparable and hashable"
    );
    bool already_initialized = has_resource<State<StateType>>() &&
                               has_resource<NextState<StateType>>();

    auto& current_state =
        add_resource(State<StateType> {std::forward<T>(state)});
    add_resource(NextState<StateType> {});
    if (!has_resource<CommandsQueue>()) {
        add_resource(CommandsQueue {});
    }

    if (!already_initialized) {
        add_systems(StateTransitionSchedule, apply_state_transition<StateType>);
    }

    return current_state;
}

inline void World::run_state_transitions() {
    run_schedule(StateTransitionSchedule);
}

} // namespace fei
