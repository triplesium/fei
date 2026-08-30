#pragma once

#include "refl/conversion.hpp"
#include "refl/reflected_conversion.hpp"

#include <optional>
#include <string>
#include <type_traits>

namespace ets {

namespace detail {
template<typename T>
struct ArgumentAccessTraits {
    using NoRef = std::remove_reference_t<T>;
    using NoPtr = std::remove_pointer_t<NoRef>;
    using Base = std::remove_cv_t<NoPtr>;
};

template<typename T>
constexpr bool needs_mutable_argument() {
    using Traits = ArgumentAccessTraits<T>;
    return (std::is_pointer_v<typename Traits::NoRef> &&
            !std::is_const_v<typename Traits::NoPtr>) ||
           std::is_rvalue_reference_v<T> ||
           (std::is_lvalue_reference_v<T> &&
            !std::is_const_v<typename Traits::NoRef>) ||
           (!std::is_reference_v<T> &&
            !std::is_copy_constructible_v<typename Traits::Base>);
}

template<typename T>
constexpr bool can_bind_converted_value() {
    using Traits = ArgumentAccessTraits<T>;
    using Base = typename Traits::Base;
    return !std::is_reference_v<T> ||
           (std::is_enum_v<Base> &&
            !std::is_pointer_v<typename Traits::NoRef> &&
            !(std::is_lvalue_reference_v<T> &&
              !std::is_const_v<typename Traits::NoRef>));
}

} // namespace detail

template<class T>
struct ArgumentAdapter {
  private:
    static constexpr bool supports_reflected_conversion() {
        using Traits = detail::ArgumentAccessTraits<T>;
        using NoRef = typename Traits::NoRef;
        return !std::is_pointer_v<NoRef> &&
               !(std::is_lvalue_reference_v<T> && !std::is_const_v<NoRef>);
    }

    static ConversionRank direct_match(const Ref& ref) {
        using Traits = detail::ArgumentAccessTraits<T>;
        using NoRef = typename Traits::NoRef;
        using NoPtr = typename Traits::NoPtr;
        using Base = typename Traits::Base;

        if (!ref) {
            return ConversionRank::None;
        }

        if constexpr (std::is_pointer_v<NoRef>) {
            if (ref.type_id() != ets::type_id<Base>()) {
                return ConversionRank::None;
            }
            return (std::is_const_v<NoPtr> || !ref.is_const()) ?
                       ConversionRank::Exact :
                       ConversionRank::None;
        } else if constexpr (
            std::is_lvalue_reference_v<T> && !std::is_const_v<NoRef>
        ) {
            return (ref.type_id() == ets::type_id<Base>() && !ref.is_const()) ?
                       ConversionRank::Exact :
                       ConversionRank::None;
        } else if constexpr (
            std::is_rvalue_reference_v<T> ||
            (!std::is_reference_v<T> && !std::is_copy_constructible_v<Base>)
        ) {
            if constexpr (std::is_enum_v<Base>) {
                return Conversion<Base>::match(ref);
            } else {
                return (ref.type_id() == ets::type_id<Base>() &&
                        !ref.is_const()) ?
                           ConversionRank::Exact :
                           ConversionRank::None;
            }
        } else if constexpr (detail::can_bind_converted_value<T>()) {
            return Conversion<Base>::match(ref);
        } else {
            return (ref.type_id() == ets::type_id<Base>() &&
                    (!detail::needs_mutable_argument<T>() || !ref.is_const())) ?
                       ConversionRank::Exact :
                       ConversionRank::None;
        }
    }

  public:
    static ConversionRank match(const Ref& ref) {
        const auto direct = direct_match(ref);
        if (direct != ConversionRank::None) {
            return direct;
        }
        if constexpr (supports_reflected_conversion()) {
            using Base = typename detail::ArgumentAccessTraits<T>::Base;
            return reflected_conversion_rank(ets::type_id<Base>(), ref);
        }
        return ConversionRank::None;
    }

    static bool requires_reflected_conversion(const Ref& ref) {
        if constexpr (supports_reflected_conversion()) {
            using Base = typename detail::ArgumentAccessTraits<T>::Base;
            return direct_match(ref) == ConversionRank::None &&
                   reflected_conversion_rank(ets::type_id<Base>(), ref) !=
                       ConversionRank::None;
        }
        return false;
    }

    static bool accepts(const Ref& ref) {
        return match(ref) != ConversionRank::None;
    }

    static decltype(auto) get(const Ref& ref) {
        using Traits = detail::ArgumentAccessTraits<T>;
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
            std::is_lvalue_reference_v<T> && !std::is_const_v<NoRef>
        ) {
            return ref.get<Base>();
        } else if constexpr (std::is_enum_v<Base>) {
            return Conversion<Base>::get(ref);
        } else if constexpr (std::is_lvalue_reference_v<T>) {
            return ref.get_const<Base>();
        } else if constexpr (
            std::is_rvalue_reference_v<T> || !std::is_copy_constructible_v<Base>
        ) {
            return ref.get_rref<Base>();
        } else {
            return Conversion<Base>::get(ref);
        }
    }

    static std::string expected_type() { return std::string(type_name<T>()); }

    static std::string actual_type(const Ref& ref) {
        return detail::describe_ref(ref);
    }

    static std::string describe_mismatch(const Ref& ref) {
        return "expected " + expected_type() + ", got " + actual_type(ref);
    }
};

template<class T>
std::optional<InvokeFailure>
prepare_reflected_argument(Ref& argument, Val& storage) {
    if constexpr (requires {
                      ArgumentAdapter<T>::requires_reflected_conversion(
                          argument
                      );
                  }) {
        if (ArgumentAdapter<T>::requires_reflected_conversion(argument)) {
            using Base = typename detail::ArgumentAccessTraits<T>::Base;
            auto converted = reflected_convert(ets::type_id<Base>(), argument);
            if (!converted) {
                return std::move(converted.error());
            }
            storage = std::move(*converted);
            argument = storage.ref();
        }
    }
    return std::nullopt;
}

} // namespace ets
