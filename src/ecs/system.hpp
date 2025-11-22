#pragma once

#include "base/type_traits.hpp"

#include <concepts>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>

namespace fei {

class World;

template<typename T>
concept StatefulSystemParam = requires(World& world) {
    typename T::State;
    { T::init_state(world) } -> std::same_as<typename T::State>;
    {
        T::get_param(world, std::declval<typename T::State&>())
    } -> std::same_as<T>;
};

template<typename T>
concept StatelessSystemParam = requires(World& world) {
    { T::get_param(world) } -> std::same_as<T>;
};

template<typename T>
concept SystemParam = StatefulSystemParam<T> || StatelessSystemParam<T>;

struct Dependency {};

struct Condition {};

struct SystemConfig {
    std::vector<Dependency> dependencies;
    std::vector<Condition> conditions;
};

// Concept to check if a type can be used as a system
template<typename T>
concept IntoSystem =
    // System is a function (pointer)
    ((std::is_function_v<std::remove_pointer_t<T>> ||
      // Or a callable object
      (std::is_class_v<T> && requires { &T::operator(); })) &&
     // System should not return value
     std::is_same_v<void, typename fei::FunctionTraits<T>::return_type> &&
     // All arguments must be a SystemParam
     []<typename... Ts>(std::type_identity<std::tuple<Ts...>>) {
         return (SystemParam<Ts> && ...);
     }(std::type_identity<typename fei::FunctionTraits<T>::args_tuple>()));

class System {
  private:
    SystemConfig m_config;

  public:
    System() = default;
    virtual ~System() = default;

    virtual void run(World& world) = 0;
};

template<typename Func>
class FunctionSystem : public System {
  private:
    using ParamTypes = typename FunctionTraits<Func>::args_tuple;
    Func m_func;

    template<typename T>
    struct ParamState {
        using type = std::monostate;
    };

    template<StatefulSystemParam T>
    struct ParamState<T> {
        using type = typename T::State;
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
        if constexpr (StatefulSystemParam<T>) {
            return T::get_param(world, std::get<I>(m_states));
        } else if constexpr (StatelessSystemParam<T>) {
            return T::get_param(world);
        } else {
            static_assert(false, "Unsupported system parameter type");
        }
    }
};

} // namespace fei
