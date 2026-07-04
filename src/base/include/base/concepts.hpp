#pragma once
#include <type_traits>

namespace fei {

namespace detail {
template<class T, template<class...> class Template>
struct SpecializationOfImpl : std::false_type {};

template<template<class...> class Template, class... Args>
struct SpecializationOfImpl<Template<Args...>, Template> : std::true_type {};
} // namespace detail

template<class T, template<class...> class Template>
concept SpecializationOf = detail::SpecializationOfImpl<T, Template>::value;

template<class T, class... Ts>
concept AnyOf = (std::is_same_v<T, Ts> || ...);
} // namespace fei
