#pragma once

#include "refl/ref.hpp"
#include "refl/type.hpp"

#include <type_traits>
#include <utility>

namespace fei::detail {

template<typename T>
struct ArgBindingTraits {
    using NoRef = std::remove_reference_t<T>;
    using NoPtr = std::remove_pointer_t<NoRef>;
    using Base = std::remove_cv_t<NoPtr>;
};

template<typename T>
constexpr bool accepts_enum_int_arg() {
    using Traits = ArgBindingTraits<T>;
    return std::is_enum_v<typename Traits::Base> &&
           !std::is_pointer_v<typename Traits::NoRef> &&
           !(std::is_lvalue_reference_v<T> &&
             !std::is_const_v<typename Traits::NoRef>);
}

template<typename T>
constexpr bool needs_mutable_arg() {
    using Traits = ArgBindingTraits<T>;
    return (std::is_pointer_v<typename Traits::NoRef> &&
            !std::is_const_v<typename Traits::NoPtr>) ||
           std::is_rvalue_reference_v<T> ||
           (std::is_lvalue_reference_v<T> &&
            !std::is_const_v<typename Traits::NoRef>) ||
           (!std::is_reference_v<T> &&
            !std::copy_constructible<typename Traits::Base>);
}

template<typename T>
bool can_bind_arg(const Ref& ref) {
    using Base = typename ArgBindingTraits<T>::Base;

    if (!ref) {
        return false;
    }
    if constexpr (accepts_enum_int_arg<T>()) {
        if (ref.type_id() == type_id<int>()) {
            return true;
        }
    }
    if (ref.type_id() != type_id<Base>()) {
        return false;
    }
    return !needs_mutable_arg<T>() || !ref.is_const();
}

template<typename T>
decltype(auto) ref_to_arg(const Ref& ref) {
    using Traits = ArgBindingTraits<T>;
    using NoRef = typename Traits::NoRef;
    using NoPtr = typename Traits::NoPtr;
    using Base = typename Traits::Base;

    if constexpr (std::is_pointer_v<NoRef>) {
        if constexpr (std::is_const_v<NoPtr>) {
            return ref.try_get_const<std::remove_const_t<NoPtr>>();
        } else {
            return ref.try_get<NoPtr>();
        }
    } else if constexpr (
        std::is_enum_v<Base> && std::is_lvalue_reference_v<T> &&
        !std::is_const_v<NoRef>) {
        return ref.get<Base>();
    } else if constexpr (std::is_enum_v<Base>) {
        Base value = ref.type_id() == type_id<int>() ?
                         static_cast<Base>(ref.get_const<int>()) :
                         ref.get_const<Base>();
        return value;
    } else if constexpr (std::is_lvalue_reference_v<T>) {
        if constexpr (std::is_const_v<NoRef>) {
            return ref.get_const<Base>();
        } else {
            return ref.get<Base>();
        }
    } else if constexpr (
        std::is_rvalue_reference_v<T> || !std::is_copy_constructible_v<Base>) {
        return std::move(ref.get<Base>());
    } else {
        return ref.get_const<Base>();
    }
}

} // namespace fei::detail
