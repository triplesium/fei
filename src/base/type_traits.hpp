#pragma once
#include <functional>
#include <tuple>
#include <type_traits>

namespace fei {

template<typename T>
struct FunctionTraits : public FunctionTraits<decltype(&T::operator())> {};

template<typename T>
    requires std::is_reference_v<T> || std::is_pointer_v<T>
struct FunctionTraits<T> : public FunctionTraits<std::remove_cvref_t<T>> {};

template<typename ClassType, typename ReturnType, typename... Args>
struct FunctionTraits<ReturnType (ClassType::*)(Args...) const> {
    using return_type = ReturnType;
    constexpr static auto arg_size = sizeof...(Args);

    using args_tuple = std::tuple<Args...>;
    template<size_t i>
    using arg_type = typename std::tuple_element<i, std::tuple<Args...>>::type;
};

template<typename ReturnType, typename... Args>
struct FunctionTraits<ReturnType(Args...)> {
    using return_type = ReturnType;
    constexpr static auto arg_size = sizeof...(Args);

    using args_tuple = std::tuple<Args...>;
    template<size_t i>
    using arg_type = typename std::tuple_element<i, std::tuple<Args...>>::type;
    using function_pointer_type = ReturnType (*)(Args...);
};

template<typename ReturnType, typename... Args>
struct FunctionTraits<ReturnType (*)(Args...)>
    : public FunctionTraits<ReturnType(Args...)> {};

template<typename ReturnType, typename... Args>
struct FunctionTraits<ReturnType (*&)(Args...)>
    : public FunctionTraits<ReturnType(Args...)> {};

template<typename ReturnType, typename... Args>
struct FunctionTraits<std::function<ReturnType(Args...)>>
    : public FunctionTraits<ReturnType(Args...)> {};

namespace detail {
template<typename T, typename... Ts>
struct IndexInPackImpl;

template<typename T, typename... Ts>
struct IndexInPackImpl<T, T, Ts...> : std::integral_constant<std::size_t, 0> {};

template<typename T, typename U, typename... Ts>
struct IndexInPackImpl<T, U, Ts...>
    : std::
          integral_constant<std::size_t, 1 + IndexInPackImpl<T, Ts...>::value> {
};
} // namespace detail

template<typename T, typename... Ts>
constexpr std::size_t IndexInPack = detail::IndexInPackImpl<T, Ts...>::value;

} // namespace fei
