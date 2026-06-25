#pragma once

#include "base/optional.hpp"
#include "ecs/commands.hpp"
#include "ecs/system_params.hpp"
#include "refl/type.hpp"

#include <concepts>
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
void apply_state_transition(
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

    template<typename U>
    friend void apply_state_transition(
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

template<typename T>
void apply_state_transition(
    ResRW<State<T>> state,
    ResRW<NextState<T>> next_state
) {
    auto pending = next_state->take();
    if (pending) {
        state->set(std::move(*pending));
    }
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
    using StateValue = std::remove_cvref_t<T>;
    bool already_initialized = has_resource<State<StateValue>>() &&
                               has_resource<NextState<StateValue>>();

    auto& current_state =
        add_resource(State<StateValue> {std::forward<T>(state)});
    add_resource(NextState<StateValue> {});
    if (!has_resource<CommandsQueue>()) {
        add_resource(CommandsQueue {});
    }

    if (!already_initialized) {
        add_systems(
            StateTransitionSchedule,
            apply_state_transition<StateValue>
        );
    }

    return current_state;
}

inline void World::run_state_transitions() {
    run_schedule(StateTransitionSchedule);
}

} // namespace fei
