#pragma once
#include "base/log.hpp"
#include "base/type_traits.hpp"
#include "ecs/system_access.hpp"

#include <concepts>
#include <optional>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>

namespace fei {

class World;
class System;

template<typename T>
struct SystemParamTraits;

template<typename T>
struct StatelessParamTraits {
    using State = std::monostate;

    static State init_state(World&) { return {}; }

    static T get_param(World& world, State&) { return T::get_param(world); }
};

template<typename T>
struct StatefulParamTraits {
    using State = typename T::State;

    static State init_state(World& world) { return T::init_state(world); }

    static T get_param(World& world, State& state) {
        return T::get_param(world, state);
    }
};

template<typename T>
concept SystemParam =
    requires(World& world, typename SystemParamTraits<T>::State& state) {
        typename SystemParamTraits<T>::State;
        {
            SystemParamTraits<T>::init_state(world)
        } -> std::same_as<typename SystemParamTraits<T>::State>;
        { SystemParamTraits<T>::get_param(world, state) } -> std::same_as<T>;
    };

template<typename T>
concept ConditionParam =
    SystemParam<T> && IsReadOnlyConditionParam<std::remove_cvref_t<T>>::value;

// Concept to check if a type can be used as a system
template<typename T>
concept IntoSystem =
    // System is a function (pointer)
    ((std::is_function_v<std::remove_reference_t<std::remove_pointer_t<T>>> ||
      // Or a callable object
      (std::is_class_v<T> && requires { &T::operator(); })) &&
     // System should not return value
     std::is_same_v<void, typename fei::FunctionTraits<T>::return_type> &&
     // All arguments must be a SystemParam
     []<typename... Ts>(std::type_identity<std::tuple<Ts...>>) {
         return (SystemParam<Ts> && ...);
     }(std::type_identity<typename fei::FunctionTraits<T>::args_tuple>()));

template<typename T>
concept IntoCondition =
    // Condition is a function (pointer)
    ((std::is_function_v<std::remove_reference_t<std::remove_pointer_t<T>>> ||
      // Or a callable object
      (std::is_class_v<std::remove_cvref_t<T>> &&
       requires { &std::remove_cvref_t<T>::operator(); })) &&
     // Condition should return bool
     std::is_same_v<bool, typename fei::FunctionTraits<T>::return_type> &&
     // All arguments must be read-only condition params
     []<typename... Ts>(std::type_identity<std::tuple<Ts...>>) {
         return (ConditionParam<Ts> && ...);
     }(std::type_identity<typename fei::FunctionTraits<T>::args_tuple>()));

class System {
  public:
    System() = default;
    virtual ~System() = default;

    virtual void run(World& world) = 0;
    virtual const SystemAccess& access() const = 0;
    virtual bool has_profile_key() const { return false; }
    virtual std::size_t profile_key() const { return 0; }
};

template<typename Func>
class FunctionSystem : public System {
  private:
    using ParamTypes = typename FunctionTraits<Func>::args_tuple;
    static constexpr bool HasProfileKey =
        std::is_pointer_v<Func> &&
        std::is_function_v<std::remove_pointer_t<Func>>;
    Func m_func;
    SystemAccess m_access {system_access_for_params<ParamTypes>()};

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
    explicit FunctionSystem(Func func) : m_func(func) {}

    void run(World& world) override {
        auto params = prepare_params<ParamTypes>(world);
        std::apply(m_func, params);
    }

    const SystemAccess& access() const override { return m_access; }

    bool has_profile_key() const override { return HasProfileKey; }

    std::size_t profile_key() const override {
        if constexpr (HasProfileKey) {
            return reinterpret_cast<std::size_t>(m_func);
        }
        fei::fatal("Cannot get a profile key for non-function pointer systems");
        return 0;
    }

  private:
    template<typename Tuple>
    Tuple prepare_params(World& world) {
        return prepare_params_impl<Tuple>(
            world,
            std::make_index_sequence<std::tuple_size_v<Tuple>> {}
        );
    }

    template<typename Tuple, std::size_t... Is>
    Tuple prepare_params_impl(World& world, std::index_sequence<Is...>) {
        return std::forward_as_tuple(
            prepare_param<std::tuple_element_t<Is, Tuple>, Is>(world)...
        );
    }

    template<typename T, size_t I>
    T prepare_param(World& world) {
        using Traits = SystemParamTraits<T>;
        auto& state = std::get<I>(m_states);
        if (!state) {
            state.emplace(Traits::init_state(world));
        }
        return Traits::get_param(world, *state);
    }
};

class Condition {
  public:
    Condition() = default;
    virtual ~Condition() = default;

    virtual bool run(World& world) = 0;
    virtual const SystemAccess& access() const = 0;
};

template<typename Func>
class FunctionCondition : public Condition {
  private:
    using ParamTypes = typename FunctionTraits<Func>::args_tuple;
    Func m_func;
    SystemAccess m_access {system_access_for_params<ParamTypes>()};

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
    explicit FunctionCondition(Func func) : m_func(std::move(func)) {}

    bool run(World& world) override {
        auto params = prepare_params<ParamTypes>(world);
        return std::apply(m_func, params);
    }

    const SystemAccess& access() const override { return m_access; }

  private:
    template<typename Tuple>
    Tuple prepare_params(World& world) {
        return prepare_params_impl<Tuple>(
            world,
            std::make_index_sequence<std::tuple_size_v<Tuple>> {}
        );
    }

    template<typename Tuple, std::size_t... Is>
    Tuple prepare_params_impl(World& world, std::index_sequence<Is...>) {
        return std::forward_as_tuple(
            prepare_param<std::tuple_element_t<Is, Tuple>, Is>(world)...
        );
    }

    template<typename T, size_t I>
    T prepare_param(World& world) {
        using Traits = SystemParamTraits<T>;
        auto& state = std::get<I>(m_states);
        if (!state) {
            state.emplace(Traits::init_state(world));
        }
        return Traits::get_param(world, *state);
    }
};

} // namespace fei
