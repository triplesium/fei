#pragma once
#include <tuple>
#include <type_traits>

namespace fei {
template<class T, template<class...> class Template>
struct is_specialization_impl : std::false_type {};

template<template<class...> class Template, class... Args>
struct is_specialization_impl<Template<Args...>, Template> : std::true_type {};

template<class T, template<class...> class Template>
concept is_specialization = is_specialization_impl<T, Template>::value;

template<class T, class... Ts>
concept is_any_of = (std::is_same_v<T, Ts> || ...);

template<typename T>
struct function_traits : public function_traits<decltype(&T::operator())> {};

template<typename ClassType, typename ReturnType, typename... Args>
struct function_traits<ReturnType (ClassType::*)(Args...) const> {
    using return_type = ReturnType;
    constexpr static auto arg_size = sizeof...(Args);

    using args_tuple = std::tuple<Args...>;
    template<size_t i>
    using arg_type = typename std::tuple_element<i, std::tuple<Args...>>::type;
};

template<typename ReturnType, typename... Args>
struct function_traits<ReturnType(Args...)> {
    using return_type = ReturnType;
    constexpr static auto arg_size = sizeof...(Args);

    using args_tuple = std::tuple<Args...>;
    template<size_t i>
    using arg_type = typename std::tuple_element<i, std::tuple<Args...>>::type;
};

template<typename ReturnType, typename... Args>
struct function_traits<ReturnType (*)(Args...)>
    : public function_traits<ReturnType(Args...)> {};

template<typename ReturnType, typename... Args>
struct function_traits<ReturnType (*&)(Args...)>
    : public function_traits<ReturnType(Args...)> {};

template<typename T, typename... Ts>
struct index_in_pack_impl;

template<typename T, typename... Ts>
struct index_in_pack_impl<T, T, Ts...>
    : std::integral_constant<std::size_t, 0> {};

template<typename T, typename U, typename... Ts>
struct index_in_pack_impl<T, U, Ts...>
    : std::integral_constant<
          std::size_t,
          1 + index_in_pack_impl<T, Ts...>::value> {};

template<typename T, typename... Ts>
constexpr std::size_t index_in_pack = index_in_pack_impl<T, Ts...>::value;
} // namespace fei
