#pragma once
#include "base/log.hpp"
#include "base/result.hpp"
#include "base/type_traits.hpp"
#include "ecs/change_detection.hpp"
#include "ecs/runtime_state.hpp"
#include "ecs/system_access.hpp"

#include <concepts>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace ets {

class World;
class System;

template<typename T>
struct SystemParamTraits;

template<typename T>
struct StatelessParamTraits {
    using State = std::monostate;

    static State init_state(World&) { return {}; }

    static T get_param(World& world, State&, SystemTicks) {
        return T::get_param(world);
    }
};

template<typename T>
struct StatefulParamTraits {
    using State = typename T::State;

    static State init_state(World& world) { return T::init_state(world); }

    static T get_param(World& world, State& state, SystemTicks) {
        return T::get_param(world, state);
    }
};

template<typename T>
concept SystemParam = requires(
    World& world,
    typename SystemParamTraits<T>::State& state,
    SystemTicks system_ticks
) {
    typename SystemParamTraits<T>::State;
    {
        SystemParamTraits<T>::init_state(world)
    } -> std::same_as<typename SystemParamTraits<T>::State>;
    {
        SystemParamTraits<T>::get_param(world, state, system_ticks)
    } -> std::same_as<T>;
};

template<typename T>
concept ConditionParam =
    SystemParam<T> && IsReadOnlyConditionParam<std::remove_cvref_t<T>>::value;

namespace detail {

template<typename Param>
Result<SystemParamRuntimeState, RuntimeStateError>
capture_system_param_runtime_state(
    const std::optional<typename SystemParamTraits<Param>::State>& state
) {
    using Traits = SystemParamTraits<Param>;
    using State = typename Traits::State;
    if (!state) {
        return SystemParamRuntimeState {};
    }
    if constexpr (std::same_as<State, std::monostate>) {
        return SystemParamRuntimeState {
            .kind = SystemParamRuntimeStateKind::Stateless,
        };
    } else if constexpr (std::same_as<State, std::size_t>) {
        return SystemParamRuntimeState {
            .kind = SystemParamRuntimeStateKind::Counter,
            .value = static_cast<std::uint64_t>(*state),
        };
    } else if constexpr (requires(const State& value) {
                             {
                                 Traits::capture_runtime_state(value)
                             } -> std::same_as<Result<
                                 SystemParamRuntimeState,
                                 RuntimeStateError>>;
                         }) {
        return Traits::capture_runtime_state(*state);
    } else {
        return failure(
            RuntimeStateError {
                .message = "System parameter state has no checkpoint adapter",
            }
        );
    }
}

template<typename Param>
Status<RuntimeStateError>
validate_system_param_runtime_state(const SystemParamRuntimeState& snapshot) {
    using Traits = SystemParamTraits<Param>;
    using State = typename Traits::State;
    if (snapshot.kind == SystemParamRuntimeStateKind::Uninitialized) {
        return {};
    }
    if constexpr (std::same_as<State, std::monostate>) {
        if (snapshot.kind != SystemParamRuntimeStateKind::Stateless) {
            return failure(
                RuntimeStateError {
                    .message = "Expected stateless system parameter state",
                }
            );
        }
        return {};
    } else if constexpr (std::same_as<State, std::size_t>) {
        if (snapshot.kind != SystemParamRuntimeStateKind::Counter) {
            return failure(
                RuntimeStateError {
                    .message = "Expected cursor system parameter state",
                }
            );
        }
        return {};
    } else if constexpr (requires(const SystemParamRuntimeState& value) {
                             {
                                 Traits::validate_runtime_state(value)
                             } -> std::same_as<Status<RuntimeStateError>>;
                         }) {
        return Traits::validate_runtime_state(snapshot);
    } else {
        return failure(
            RuntimeStateError {
                .message = "System parameter state has no checkpoint adapter",
            }
        );
    }
}

template<typename Param>
Status<RuntimeStateError> restore_system_param_runtime_state(
    std::optional<typename SystemParamTraits<Param>::State>& state,
    const SystemParamRuntimeState& snapshot
) {
    using Traits = SystemParamTraits<Param>;
    using State = typename Traits::State;
    auto valid = validate_system_param_runtime_state<Param>(snapshot);
    if (!valid) {
        return valid;
    }
    if (snapshot.kind == SystemParamRuntimeStateKind::Uninitialized) {
        state.reset();
        return {};
    }
    if constexpr (std::same_as<State, std::monostate>) {
        state.emplace();
    } else if constexpr (std::same_as<State, std::size_t>) {
        state.emplace(static_cast<std::size_t>(snapshot.value));
    } else if constexpr (requires(State& value) {
                             {
                                 Traits::restore_runtime_state(value, snapshot)
                             } -> std::same_as<Status<RuntimeStateError>>;
                         } && std::default_initializable<State>) {
        if (!state) {
            state.emplace();
        }
        return Traits::restore_runtime_state(*state, snapshot);
    }
    return {};
}

template<typename ParamTypes, typename StateTuple, std::size_t... Is>
Result<std::vector<SystemParamRuntimeState>, RuntimeStateError>
capture_system_param_runtime_states(
    const StateTuple& states,
    std::index_sequence<Is...>
) {
    std::vector<SystemParamRuntimeState> snapshots;
    snapshots.reserve(sizeof...(Is));
    std::optional<RuntimeStateError> error;
    (
        [&] {
            if (error) {
                return;
            }
            auto snapshot = capture_system_param_runtime_state<
                std::tuple_element_t<Is, ParamTypes>>(std::get<Is>(states));
            if (!snapshot) {
                error = std::move(snapshot.error());
                error->path = "params[" + std::to_string(Is) + "]";
                return;
            }
            snapshots.push_back(std::move(*snapshot));
        }(),
        ...);
    if (error) {
        return failure(std::move(*error));
    }
    return snapshots;
}

template<typename ParamTypes, std::size_t... Is>
Status<RuntimeStateError> validate_system_param_runtime_states(
    const std::vector<SystemParamRuntimeState>& snapshots,
    std::index_sequence<Is...>
) {
    if (snapshots.size() != sizeof...(Is)) {
        return failure(
            RuntimeStateError {
                .path = "params",
                .message = "System parameter count changed since checkpoint",
            }
        );
    }
    std::optional<RuntimeStateError> error;
    (
        [&] {
            if (error) {
                return;
            }
            auto status = validate_system_param_runtime_state<
                std::tuple_element_t<Is, ParamTypes>>(snapshots[Is]);
            if (!status) {
                error = std::move(status.error());
                error->path = "params[" + std::to_string(Is) + "]";
            }
        }(),
        ...);
    if (error) {
        return failure(std::move(*error));
    }
    return {};
}

template<typename ParamTypes, typename StateTuple, std::size_t... Is>
Status<RuntimeStateError> restore_system_param_runtime_states(
    StateTuple& states,
    const std::vector<SystemParamRuntimeState>& snapshots,
    std::index_sequence<Is...> indices
) {
    auto valid =
        validate_system_param_runtime_states<ParamTypes>(snapshots, indices);
    if (!valid) {
        return valid;
    }
    std::optional<RuntimeStateError> error;
    (
        [&] {
            if (error) {
                return;
            }
            auto status = restore_system_param_runtime_state<
                std::tuple_element_t<Is, ParamTypes>>(
                std::get<Is>(states),
                snapshots[Is]
            );
            if (!status) {
                error = std::move(status.error());
                error->path = "params[" + std::to_string(Is) + "]";
            }
        }(),
        ...);
    if (error) {
        return failure(std::move(*error));
    }
    return {};
}

} // namespace detail

// Concept to check if a type can be used as a system
template<typename T>
concept IntoSystem =
    // System is a function (pointer)
    ((std::is_function_v<std::remove_reference_t<std::remove_pointer_t<T>>> ||
      // Or a callable object
      (std::is_class_v<T> && requires { &T::operator(); })) &&
     // System should not return value
     std::is_same_v<void, typename ets::FunctionTraits<T>::return_type> &&
     // All arguments must be a SystemParam
     []<typename... Ts>(std::type_identity<std::tuple<Ts...>>) {
         return (SystemParam<Ts> && ...);
     }(std::type_identity<typename ets::FunctionTraits<T>::args_tuple>()));

template<typename T>
concept IntoCondition =
    // Condition is a function (pointer)
    ((std::is_function_v<std::remove_reference_t<std::remove_pointer_t<T>>> ||
      // Or a callable object
      (std::is_class_v<std::remove_cvref_t<T>> &&
       requires { &std::remove_cvref_t<T>::operator(); })) &&
     // Condition should return bool
     std::is_same_v<bool, typename ets::FunctionTraits<T>::return_type> &&
     // All arguments must be read-only condition params
     []<typename... Ts>(std::type_identity<std::tuple<Ts...>>) {
         return (ConditionParam<Ts> && ...);
     }(std::type_identity<typename ets::FunctionTraits<T>::args_tuple>()));

class System {
  private:
    Tick m_last_run {0};
    Tick m_current_run {0};
    bool m_running {false};
    std::optional<SystemRuntimeState> m_pending_runtime_state;

  public:
    System() = default;
    virtual ~System() = default;

    void run(World& world);
    virtual const SystemAccess& access() const = 0;
    virtual void queue_deferred(CommandsQueue&) {}
    virtual bool has_profile_key() const { return false; }
    virtual std::size_t profile_key() const { return 0; }

    Result<SystemRuntimeState, RuntimeStateError> capture_runtime_state() const;
    Status<RuntimeStateError>
    validate_runtime_state(const SystemRuntimeState& state) const;
    Status<RuntimeStateError>
    restore_runtime_state(const SystemRuntimeState& state);

  protected:
    virtual void execute(World& world, SystemTicks system_ticks) = 0;
    virtual Result<SystemExecutorRuntimeState, RuntimeStateError>
    capture_executor_runtime_state() const;
    virtual Status<RuntimeStateError> validate_executor_runtime_state(
        const SystemExecutorRuntimeState& state
    ) const;
    virtual Status<RuntimeStateError>
    restore_executor_runtime_state(const SystemExecutorRuntimeState& state);
    virtual Result<std::vector<SystemParamRuntimeState>, RuntimeStateError>
    capture_param_runtime_states() const {
        return std::vector<SystemParamRuntimeState> {};
    }
    virtual Status<RuntimeStateError> validate_param_runtime_states(
        const std::vector<SystemParamRuntimeState>& states
    ) const;
    virtual Status<RuntimeStateError> restore_param_runtime_states(
        const std::vector<SystemParamRuntimeState>& states
    );
};

template<typename Func, bool CheckpointCallableState = false>
class FunctionSystem : public System {
  private:
    static_assert(
        !CheckpointCallableState || std::copy_constructible<Func>,
        "Checkpointed system callables must be copy constructible"
    );
    using ParamTypes = typename FunctionTraits<Func>::args_tuple;
    using FuncStorage = std::
        conditional_t<CheckpointCallableState, std::unique_ptr<Func>, Func>;
    static constexpr bool HasProfileKey =
        std::is_pointer_v<Func> &&
        std::is_function_v<std::remove_pointer_t<Func>>;
    static constexpr bool CallableIsStateless =
        HasProfileKey || (std::is_class_v<Func> && std::is_empty_v<Func>);
    FuncStorage m_func;
    SystemAccess m_access {system_access_for_params<ParamTypes>()};

    static FuncStorage make_func_storage(Func func) {
        if constexpr (CheckpointCallableState) {
            return std::make_unique<Func>(std::move(func));
        } else {
            return std::move(func);
        }
    }

    Func& callable() {
        if constexpr (CheckpointCallableState) {
            return *m_func;
        } else {
            return m_func;
        }
    }

    const Func& callable() const {
        if constexpr (CheckpointCallableState) {
            return *m_func;
        } else {
            return m_func;
        }
    }

    template<typename T>
    struct ParamState {
        using type = std::optional<typename SystemParamTraits<T>::State>;
    };

    template<typename Tuple>
    struct StateTupleGen;

    template<typename... Ts>
    struct StateTupleGen<std::tuple<Ts...>> {
        using type = std::tuple<typename ParamState<Ts>::type...>;
    };

    using StateTuple = typename StateTupleGen<ParamTypes>::type;
    StateTuple m_states;

  public:
    explicit FunctionSystem(Func func) :
        m_func(make_func_storage(std::move(func))) {}

    void execute(World& world, SystemTicks system_ticks) override {
        auto params = prepare_params<ParamTypes>(world, system_ticks);
        std::apply(callable(), params);
    }

    const SystemAccess& access() const override { return m_access; }

    void queue_deferred(CommandsQueue& target) override {
        queue_deferred_impl(
            target,
            std::make_index_sequence<std::tuple_size_v<ParamTypes>> {}
        );
    }

    bool has_profile_key() const override { return HasProfileKey; }

    std::size_t profile_key() const override {
        if constexpr (HasProfileKey) {
            return reinterpret_cast<std::size_t>(callable());
        }
        ets::fatal("Cannot get a profile key for non-function pointer systems");
        return 0;
    }

  protected:
    Result<SystemExecutorRuntimeState, RuntimeStateError>
    capture_executor_runtime_state() const override {
        if constexpr (CheckpointCallableState) {
            return SystemExecutorRuntimeState::from_value(callable());
        } else if constexpr (CallableIsStateless) {
            return SystemExecutorRuntimeState::stateless();
        } else {
            return failure(
                RuntimeStateError {
                    .message =
                        "Stateful system callable has no checkpoint adapter; "
                        "wrap it with checkpointed_system()",
                }
            );
        }
    }

    Status<RuntimeStateError> validate_executor_runtime_state(
        const SystemExecutorRuntimeState& state
    ) const override {
        if constexpr (CheckpointCallableState) {
            if (state.kind != SystemExecutorRuntimeStateKind::Value ||
                state.value.template try_get<Func>() == nullptr) {
                return failure(
                    RuntimeStateError {
                        .message =
                            "Expected copied system callable checkpoint state",
                    }
                );
            }
            return {};
        } else if constexpr (CallableIsStateless) {
            if (state.kind != SystemExecutorRuntimeStateKind::Stateless) {
                return failure(
                    RuntimeStateError {
                        .message = "Expected stateless system callable state",
                    }
                );
            }
            return {};
        } else {
            return failure(
                RuntimeStateError {
                    .message =
                        "Stateful system callable has no checkpoint adapter; "
                        "wrap it with checkpointed_system()",
                }
            );
        }
    }

    Status<RuntimeStateError> restore_executor_runtime_state(
        const SystemExecutorRuntimeState& state
    ) override {
        auto valid = validate_executor_runtime_state(state);
        if (!valid) {
            return valid;
        }
        if constexpr (CheckpointCallableState) {
            const auto* snapshot = state.value.template try_get<Func>();
            m_func = std::make_unique<Func>(*snapshot);
        }
        return {};
    }

    Result<std::vector<SystemParamRuntimeState>, RuntimeStateError>
    capture_param_runtime_states() const override {
        return detail::capture_system_param_runtime_states<ParamTypes>(
            m_states,
            std::make_index_sequence<std::tuple_size_v<ParamTypes>> {}
        );
    }

    Status<RuntimeStateError> validate_param_runtime_states(
        const std::vector<SystemParamRuntimeState>& states
    ) const override {
        return detail::validate_system_param_runtime_states<ParamTypes>(
            states,
            std::make_index_sequence<std::tuple_size_v<ParamTypes>> {}
        );
    }

    Status<RuntimeStateError> restore_param_runtime_states(
        const std::vector<SystemParamRuntimeState>& states
    ) override {
        return detail::restore_system_param_runtime_states<ParamTypes>(
            m_states,
            states,
            std::make_index_sequence<std::tuple_size_v<ParamTypes>> {}
        );
    }

  private:
    template<typename Tuple>
    Tuple prepare_params(World& world, SystemTicks system_ticks) {
        return prepare_params_impl<Tuple>(
            world,
            system_ticks,
            std::make_index_sequence<std::tuple_size_v<Tuple>> {}
        );
    }

    template<typename Tuple, std::size_t... Is>
    Tuple prepare_params_impl(
        World& world,
        SystemTicks system_ticks,
        std::index_sequence<Is...>
    ) {
        return std::forward_as_tuple(
            prepare_param<std::tuple_element_t<Is, Tuple>, Is>(
                world,
                system_ticks
            )...
        );
    }

    template<typename T, size_t I>
    T prepare_param(World& world, SystemTicks system_ticks) {
        using Traits = SystemParamTraits<T>;
        auto& state = std::get<I>(m_states);
        if (!state) {
            state.emplace(Traits::init_state(world));
        }
        return Traits::get_param(world, *state, system_ticks);
    }

    template<std::size_t... Is>
    void
    queue_deferred_impl(CommandsQueue& target, std::index_sequence<Is...>) {
        (queue_param_deferred<std::tuple_element_t<Is, ParamTypes>, Is>(target),
         ...);
    }

    template<typename T, std::size_t I>
    void queue_param_deferred(CommandsQueue& target) {
        using Traits = SystemParamTraits<T>;
        auto& state = std::get<I>(m_states);
        if constexpr (requires(typename Traits::State& value) {
                          Traits::queue_deferred(value, target);
                      }) {
            if (state) {
                Traits::queue_deferred(*state, target);
            }
        }
    }
};

class Condition {
  private:
    Tick m_last_run {0};
    Tick m_current_run {0};
    bool m_running {false};
    std::optional<SystemRuntimeState> m_pending_runtime_state;

  public:
    Condition() = default;
    virtual ~Condition() = default;

    bool run(World& world);
    virtual const SystemAccess& access() const = 0;

    Result<SystemRuntimeState, RuntimeStateError> capture_runtime_state() const;
    Status<RuntimeStateError>
    validate_runtime_state(const SystemRuntimeState& state) const;
    Status<RuntimeStateError>
    restore_runtime_state(const SystemRuntimeState& state);

  protected:
    virtual bool evaluate(World& world, SystemTicks system_ticks) = 0;
    virtual Result<SystemExecutorRuntimeState, RuntimeStateError>
    capture_executor_runtime_state() const;
    virtual Status<RuntimeStateError> validate_executor_runtime_state(
        const SystemExecutorRuntimeState& state
    ) const;
    virtual Status<RuntimeStateError>
    restore_executor_runtime_state(const SystemExecutorRuntimeState& state);
    virtual Result<std::vector<SystemParamRuntimeState>, RuntimeStateError>
    capture_param_runtime_states() const {
        return std::vector<SystemParamRuntimeState> {};
    }
    virtual Status<RuntimeStateError> validate_param_runtime_states(
        const std::vector<SystemParamRuntimeState>& states
    ) const;
    virtual Status<RuntimeStateError> restore_param_runtime_states(
        const std::vector<SystemParamRuntimeState>& states
    );
};

template<typename Func, bool CheckpointCallableState = false>
class FunctionCondition : public Condition {
  private:
    static_assert(
        !CheckpointCallableState || std::copy_constructible<Func>,
        "Checkpointed condition callables must be copy constructible"
    );
    using ParamTypes = typename FunctionTraits<Func>::args_tuple;
    using FuncStorage = std::
        conditional_t<CheckpointCallableState, std::unique_ptr<Func>, Func>;
    static constexpr bool CallableIsStateless =
        (std::is_pointer_v<Func> &&
         std::is_function_v<std::remove_pointer_t<Func>>) ||
        (std::is_class_v<Func> && std::is_empty_v<Func>);
    FuncStorage m_func;
    SystemAccess m_access {system_access_for_params<ParamTypes>()};

    static FuncStorage make_func_storage(Func func) {
        if constexpr (CheckpointCallableState) {
            return std::make_unique<Func>(std::move(func));
        } else {
            return std::move(func);
        }
    }

    Func& callable() {
        if constexpr (CheckpointCallableState) {
            return *m_func;
        } else {
            return m_func;
        }
    }

    const Func& callable() const {
        if constexpr (CheckpointCallableState) {
            return *m_func;
        } else {
            return m_func;
        }
    }

    template<typename T>
    struct ParamState {
        using type = std::optional<typename SystemParamTraits<T>::State>;
    };

    template<typename Tuple>
    struct StateTupleGen;

    template<typename... Ts>
    struct StateTupleGen<std::tuple<Ts...>> {
        using type = std::tuple<typename ParamState<Ts>::type...>;
    };

    using StateTuple = typename StateTupleGen<ParamTypes>::type;
    StateTuple m_states;

  public:
    explicit FunctionCondition(Func func) :
        m_func(make_func_storage(std::move(func))) {}

    bool evaluate(World& world, SystemTicks system_ticks) override {
        auto params = prepare_params<ParamTypes>(world, system_ticks);
        return std::apply(callable(), params);
    }

    const SystemAccess& access() const override { return m_access; }

  protected:
    Result<SystemExecutorRuntimeState, RuntimeStateError>
    capture_executor_runtime_state() const override {
        if constexpr (CheckpointCallableState) {
            return SystemExecutorRuntimeState::from_value(callable());
        } else if constexpr (CallableIsStateless) {
            return SystemExecutorRuntimeState::stateless();
        } else {
            return failure(
                RuntimeStateError {
                    .message = "Stateful condition callable has no checkpoint "
                               "adapter; wrap it with "
                               "run_if_checkpointed()",
                }
            );
        }
    }

    Status<RuntimeStateError> validate_executor_runtime_state(
        const SystemExecutorRuntimeState& state
    ) const override {
        if constexpr (CheckpointCallableState) {
            if (state.kind != SystemExecutorRuntimeStateKind::Value ||
                state.value.template try_get<Func>() == nullptr) {
                return failure(
                    RuntimeStateError {
                        .message = "Expected copied condition callable "
                                   "checkpoint state",
                    }
                );
            }
            return {};
        } else if constexpr (CallableIsStateless) {
            if (state.kind != SystemExecutorRuntimeStateKind::Stateless) {
                return failure(
                    RuntimeStateError {
                        .message =
                            "Expected stateless condition callable state",
                    }
                );
            }
            return {};
        } else {
            return failure(
                RuntimeStateError {
                    .message = "Stateful condition callable has no checkpoint "
                               "adapter; wrap it with "
                               "run_if_checkpointed()",
                }
            );
        }
    }

    Status<RuntimeStateError> restore_executor_runtime_state(
        const SystemExecutorRuntimeState& state
    ) override {
        auto valid = validate_executor_runtime_state(state);
        if (!valid) {
            return valid;
        }
        if constexpr (CheckpointCallableState) {
            const auto* snapshot = state.value.template try_get<Func>();
            m_func = std::make_unique<Func>(*snapshot);
        }
        return {};
    }

    Result<std::vector<SystemParamRuntimeState>, RuntimeStateError>
    capture_param_runtime_states() const override {
        return detail::capture_system_param_runtime_states<ParamTypes>(
            m_states,
            std::make_index_sequence<std::tuple_size_v<ParamTypes>> {}
        );
    }

    Status<RuntimeStateError> validate_param_runtime_states(
        const std::vector<SystemParamRuntimeState>& states
    ) const override {
        return detail::validate_system_param_runtime_states<ParamTypes>(
            states,
            std::make_index_sequence<std::tuple_size_v<ParamTypes>> {}
        );
    }

    Status<RuntimeStateError> restore_param_runtime_states(
        const std::vector<SystemParamRuntimeState>& states
    ) override {
        return detail::restore_system_param_runtime_states<ParamTypes>(
            m_states,
            states,
            std::make_index_sequence<std::tuple_size_v<ParamTypes>> {}
        );
    }

  private:
    template<typename Tuple>
    Tuple prepare_params(World& world, SystemTicks system_ticks) {
        return prepare_params_impl<Tuple>(
            world,
            system_ticks,
            std::make_index_sequence<std::tuple_size_v<Tuple>> {}
        );
    }

    template<typename Tuple, std::size_t... Is>
    Tuple prepare_params_impl(
        World& world,
        SystemTicks system_ticks,
        std::index_sequence<Is...>
    ) {
        return std::forward_as_tuple(
            prepare_param<std::tuple_element_t<Is, Tuple>, Is>(
                world,
                system_ticks
            )...
        );
    }

    template<typename T, size_t I>
    T prepare_param(World& world, SystemTicks system_ticks) {
        using Traits = SystemParamTraits<T>;
        auto& state = std::get<I>(m_states);
        if (!state) {
            state.emplace(Traits::init_state(world));
        }
        return Traits::get_param(world, *state, system_ticks);
    }
};

} // namespace ets
