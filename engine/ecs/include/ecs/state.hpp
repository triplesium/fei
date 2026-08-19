#pragma once

#include "base/log.hpp"
#include "base/optional.hpp"
#include "ecs/commands.hpp"
#include "ecs/dynamic/state.hpp"
#include "ecs/system_config.hpp"
#include "ecs/system_params.hpp"
#include "ecs/system_set.hpp"
#include "refl/type.hpp"

#include <atomic>
#include <concepts>
#include <functional>
#include <limits>
#include <mutex>
#include <type_traits>
#include <unordered_map>
#include <utility>

namespace fei {

inline constexpr ScheduleId StateTransition =
    stable_type_hash("fei::StateTransition");

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
    std::copyable<std::remove_cvref_t<T>> &&
    std::equality_comparable<std::remove_cvref_t<T>> && HashableStateValue<T>;

namespace detail {

inline constexpr ScheduleId DynamicStateScheduleIdMask =
    ScheduleId {1} << (std::numeric_limits<ScheduleId>::digits - 1);

inline ScheduleId allocate_state_schedule_id() {
    static std::atomic<ScheduleId> next {DynamicStateScheduleIdMask};
    const auto id = next.fetch_add(1, std::memory_order_relaxed);
    if ((id & DynamicStateScheduleIdMask) == 0) {
        fatal("State schedule id space exhausted");
    }
    return id;
}

template<StateValue T>
ScheduleId intern_enter_schedule(const T& state) {
    static std::mutex mutex;
    static std::unordered_map<T, ScheduleId> schedules;

    std::scoped_lock lock(mutex);
    if (auto found = schedules.find(state); found != schedules.end()) {
        return found->second;
    }
    auto id = allocate_state_schedule_id();
    schedules.emplace(state, id);
    return id;
}

template<StateValue T>
ScheduleId intern_exit_schedule(const T& state) {
    static std::mutex mutex;
    static std::unordered_map<T, ScheduleId> schedules;

    std::scoped_lock lock(mutex);
    if (auto found = schedules.find(state); found != schedules.end()) {
        return found->second;
    }
    auto id = allocate_state_schedule_id();
    schedules.emplace(state, id);
    return id;
}

template<StateValue T>
struct StateTransitionKey {
    T from;
    T to;

    bool operator==(const StateTransitionKey&) const = default;
};

template<StateValue T>
struct StateTransitionKeyHash {
    std::size_t operator()(const StateTransitionKey<T>& key) const {
        auto seed = std::hash<T> {}(key.from);
        const auto value = std::hash<T> {}(key.to);
        return seed ^
               (value + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2));
    }
};

template<StateValue T>
ScheduleId intern_transition_schedule(const T& from, const T& to) {
    static std::mutex mutex;
    static std::unordered_map<
        StateTransitionKey<T>,
        ScheduleId,
        StateTransitionKeyHash<T>>
        schedules;

    StateTransitionKey<T> key {.from = from, .to = to};
    std::scoped_lock lock(mutex);
    if (auto found = schedules.find(key); found != schedules.end()) {
        return found->second;
    }
    auto id = allocate_state_schedule_id();
    schedules.emplace(std::move(key), id);
    return id;
}

struct StateTransitionSystems {
    struct Apply : SystemSet<Apply> {};
    struct Exit : SystemSet<Exit> {};
    struct Transition : SystemSet<Transition> {};
    struct Enter : SystemSet<Enter> {};
};

template<StateValue T>
class StateTransitionContext {
  private:
    Optional<T> m_exited;
    Optional<T> m_entered;
    bool m_initial_pending {true};
    bool m_active {false};

  public:
    StateTransitionContext() = default;
    explicit StateTransitionContext(bool initial_pending) :
        m_initial_pending(initial_pending) {}

    bool active() const { return m_active; }
    const Optional<T>& exited() const { return m_exited; }
    const Optional<T>& entered() const { return m_entered; }

    void queue_initial_enter() {
        m_exited.reset();
        m_entered.reset();
        m_initial_pending = true;
        m_active = false;
    }

    bool apply_initial_enter(const T& state) {
        if (!m_initial_pending) {
            return false;
        }
        m_initial_pending = false;
        m_exited.reset();
        m_entered = state;
        m_active = true;
        return true;
    }

    void clear() {
        m_exited.reset();
        m_entered.reset();
        m_active = false;
    }

    void set(T exited, T entered) {
        m_exited = std::move(exited);
        m_entered = std::move(entered);
        m_active = true;
    }
};

} // namespace detail

template<StateValue T>
class OnEnter {
  private:
    ScheduleId m_id;

  public:
    explicit OnEnter(const T& state) :
        m_id(detail::intern_enter_schedule(state)) {}

    ScheduleId id() const { return m_id; }
    operator ScheduleId() const { return m_id; }
};

template<StateValue T>
class OnExit {
  private:
    ScheduleId m_id;

  public:
    explicit OnExit(const T& state) :
        m_id(detail::intern_exit_schedule(state)) {}

    ScheduleId id() const { return m_id; }
    operator ScheduleId() const { return m_id; }
};

template<StateValue T>
class OnTransition {
  private:
    ScheduleId m_id;

  public:
    OnTransition(const T& exited, const T& entered) :
        m_id(detail::intern_transition_schedule(exited, entered)) {}

    ScheduleId id() const { return m_id; }
    operator ScheduleId() const { return m_id; }
};

template<StateValue T>
void prepare_state_transition(
    ResRW<State<T>> state,
    ResRW<NextState<T>> next_state,
    ResRW<detail::StateTransitionContext<T>> transition
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
    friend void prepare_state_transition(
        ResRW<State<U>> state,
        ResRW<NextState<U>> next_state,
        ResRW<detail::StateTransitionContext<U>> transition
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

namespace detail {

template<StateValue T>
void register_dynamic_state_adapter() {
    DynamicStateRegistry::instance().add(
        DynamicStateOps {
            .value_type = type_id<T>(),
            .state_resource = type_id<State<T>>(),
            .next_state_resource = type_id<NextState<T>>(),
            .initialized =
                [](const World& world) {
                    return world.has_resource<State<T>>() &&
                           world.has_resource<NextState<T>>();
                },
            .current = [](World& world) -> Ref {
                if (!world.has_resource<State<T>>()) {
                    return {};
                }
                const auto& state =
                    static_cast<const World&>(world).resource<State<T>>();
                return Ref(state.get());
            },
            .set_next = [](World& world,
                           Ref value) -> Status<DynamicSystemError> {
                if (value.type_id() != type_id<T>()) {
                    return failure(
                        DynamicSystemError {
                            "NextState value type does not match '" +
                            type_name(type_id<T>()) + "'"
                        }
                    );
                }
                if (!world.has_resource<NextState<T>>()) {
                    return failure(
                        DynamicSystemError {
                            "NextState<" + type_name(type_id<T>()) +
                            "> is not initialized"
                        }
                    );
                }
                world.resource<NextState<T>>().set(value.get_const<T>());
                return {};
            },
            .clear_next =
                [](World& world) {
                    if (world.has_resource<NextState<T>>()) {
                        world.resource<NextState<T>>().clear();
                    }
                },
            .on_enter =
                [](Ref value) {
                    return OnEnter(value.get_const<T>()).id();
                },
            .on_exit =
                [](Ref value) {
                    return OnExit(value.get_const<T>()).id();
                },
            .on_transition =
                [](Ref exited, Ref entered) {
                    return OnTransition(
                               exited.get_const<T>(),
                               entered.get_const<T>()
                    )
                        .id();
                },
        }
    );
}

} // namespace detail

template<StateValue T>
void prepare_state_transition(
    ResRW<State<T>> state,
    ResRW<NextState<T>> next_state,
    ResRW<detail::StateTransitionContext<T>> transition
) {
    auto& transition_value = *transition;
    const auto& current_state = std::as_const(state)->get();
    if (transition_value.apply_initial_enter(current_state)) {
        return;
    }

    transition_value.clear();
    auto pending = next_state->take();
    if (!pending || current_state == *pending) {
        return;
    }

    T old_state = current_state;
    T new_state = std::move(*pending);
    state->set(new_state);
    transition_value.set(std::move(old_state), std::move(new_state));
}

template<StateValue T>
void run_state_exit(
    WorldRef world,
    ResRO<detail::StateTransitionContext<T>> transition
) {
    if (transition->active() && transition->exited()) {
        world->run_schedule(OnExit(*transition->exited()));
    }
}

template<StateValue T>
void run_state_transition(
    WorldRef world,
    ResRO<detail::StateTransitionContext<T>> transition
) {
    if (transition->active() && transition->exited() && transition->entered()) {
        world->run_schedule(
            OnTransition(*transition->exited(), *transition->entered())
        );
    }
}

template<StateValue T>
void run_state_enter(
    WorldRef world,
    ResRO<detail::StateTransitionContext<T>> transition
) {
    if (transition->active() && transition->entered()) {
        world->run_schedule(OnEnter(*transition->entered()));
    }
}

template<typename T>
auto in_state(T&& expected) {
    using StateType = std::remove_cvref_t<T>;
    return [expected = StateType(std::forward<T>(expected))](
               Optional<ResRO<State<StateType>>> state
           ) {
        return state && (*state)->get() == expected;
    };
}

namespace detail {

template<StateValue T>
void install_state_runtime(World& world) {
    if (!world.has_resource<NextState<T>>()) {
        world.add_resource(NextState<T> {});
    }
    if (world.has_resource<StateTransitionContext<T>>()) {
        return;
    }

    world.add_resource(StateTransitionContext<T> {});
    if (!world.has_resource<CommandsQueue>()) {
        world.add_resource(CommandsQueue {});
    }
    world.configure_sets(
        StateTransition,
        chain(
            StateTransitionSystems::Apply {},
            StateTransitionSystems::Exit {},
            StateTransitionSystems::Transition {},
            StateTransitionSystems::Enter {}
        )
    );
    world.add_systems(
        StateTransition,
        prepare_state_transition<T> | in_set<StateTransitionSystems::Apply>(),
        run_state_exit<T> | in_set<StateTransitionSystems::Exit>(),
        run_state_transition<T> | in_set<StateTransitionSystems::Transition>(),
        run_state_enter<T> | in_set<StateTransitionSystems::Enter>()
    );
}

} // namespace detail

template<typename T>
State<std::remove_cvref_t<T>>& World::init_state(T&& initial_state) {
    using StateType = std::remove_cvref_t<T>;
    static_assert(
        fei::StateValue<StateType>,
        "State values must be copyable, equality comparable, and hashable"
    );

    detail::register_dynamic_state_adapter<StateType>();
    if (!has_resource<State<StateType>>()) {
        add_resource(
            State<StateType> {StateType(std::forward<T>(initial_state))}
        );
    }
    detail::install_state_runtime<StateType>(*this);
    return resource_untracked(type_id<State<StateType>>())
        .template get<State<StateType>>();
}

template<typename T>
State<std::remove_cvref_t<T>>& World::insert_state(T&& state) {
    using StateType = std::remove_cvref_t<T>;
    static_assert(
        fei::StateValue<StateType>,
        "State values must be copyable, equality comparable, and hashable"
    );

    detail::register_dynamic_state_adapter<StateType>();
    add_resource(State<StateType> {StateType(std::forward<T>(state))});
    detail::install_state_runtime<StateType>(*this);
    resource_untracked(type_id<NextState<StateType>>())
        .template get<NextState<StateType>>()
        .reset();
    resource_untracked(type_id<detail::StateTransitionContext<StateType>>())
        .template get<detail::StateTransitionContext<StateType>>()
        .queue_initial_enter();
    return resource_untracked(type_id<State<StateType>>())
        .template get<State<StateType>>();
}

inline void World::run_state_transitions() {
    run_schedule(StateTransition);
}

} // namespace fei
