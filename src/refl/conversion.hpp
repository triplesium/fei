#pragma once

#include "refl/ref.hpp"
#include "refl/registry.hpp"

#include <concepts>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>

namespace fei {

// Describes how costly a Ref -> T conversion is. This is intentionally
// language-neutral so method lookup, property assignment, and script bindings
// can later share the same ranking rules.
enum class ConversionRank {
    None,
    Exact,
    Weak,
};

namespace detail {

// Keep numeric conversions conservative. We allow widening-style conversions
// used by script numbers and reflected calls, but reject narrowing and
// bool-number conversions so failed calls stay visible.
template<class Source, class Target>
constexpr bool allows_integral_widening() {
    static_assert(std::is_integral_v<Source>);
    static_assert(std::is_integral_v<Target>);

    if constexpr (
        std::same_as<Source, bool> || std::same_as<Target, bool> ||
        std::same_as<Source, Target>
    ) {
        return false;
    }
    if constexpr (std::is_signed_v<Source> == std::is_signed_v<Target>) {
        return std::numeric_limits<Target>::digits >=
               std::numeric_limits<Source>::digits;
    }
    if constexpr (std::is_unsigned_v<Source> && std::is_signed_v<Target>) {
        return std::numeric_limits<Target>::digits >
               std::numeric_limits<Source>::digits;
    }
    return false;
}

template<class Source, class Target>
constexpr bool allows_integral_to_floating() {
    static_assert(std::is_integral_v<Source>);
    static_assert(std::is_floating_point_v<Target>);

    if constexpr (std::same_as<Source, bool>) {
        return false;
    } else if constexpr (std::same_as<Target, float>) {
        return sizeof(Source) <= sizeof(int);
    } else {
        return std::numeric_limits<Target>::digits >=
               std::numeric_limits<int>::digits;
    }
}

template<class Source, class Target>
constexpr bool allows_floating_widening() {
    static_assert(std::is_floating_point_v<Source>);
    static_assert(std::is_floating_point_v<Target>);

    if constexpr (std::same_as<Source, Target>) {
        return false;
    } else {
        return std::numeric_limits<Target>::digits >=
               std::numeric_limits<Source>::digits;
    }
}

template<class Source, class Target>
constexpr bool allows_numeric_conversion() {
    if constexpr (std::is_integral_v<Source> && std::is_integral_v<Target>) {
        return allows_integral_widening<Source, Target>();
    } else if constexpr (
        std::is_integral_v<Source> && std::is_floating_point_v<Target>
    ) {
        return allows_integral_to_floating<Source, Target>();
    } else if constexpr (
        std::is_floating_point_v<Source> && std::is_floating_point_v<Target>
    ) {
        return allows_floating_widening<Source, Target>();
    } else {
        return false;
    }
}

template<class Source, class Target>
bool matches_numeric_source(const Ref& ref) {
    return ref.type_id() == fei::type_id<Source>() &&
           allows_numeric_conversion<Source, Target>();
}

template<class Target>
bool matches_numeric_source(const Ref& ref) {
    return matches_numeric_source<char, Target>(ref) ||
           matches_numeric_source<signed char, Target>(ref) ||
           matches_numeric_source<unsigned char, Target>(ref) ||
           matches_numeric_source<short int, Target>(ref) ||
           matches_numeric_source<unsigned short int, Target>(ref) ||
           matches_numeric_source<int, Target>(ref) ||
           matches_numeric_source<unsigned int, Target>(ref) ||
           matches_numeric_source<long int, Target>(ref) ||
           matches_numeric_source<unsigned long int, Target>(ref) ||
           matches_numeric_source<long long int, Target>(ref) ||
           matches_numeric_source<unsigned long long int, Target>(ref) ||
           matches_numeric_source<float, Target>(ref) ||
           matches_numeric_source<double, Target>(ref) ||
           matches_numeric_source<long double, Target>(ref);
}

inline std::string describe_ref(const Ref& ref) {
    if (!ref) {
        return "<empty>";
    }

    std::string result;
    if (ref.is_const()) {
        result += "const ";
    }
    if (auto* type = Registry::instance().try_get_type(ref.type_id())) {
        result += type->name();
    } else {
        result += "type_id(" + std::to_string(ref.type_id().id()) + ")";
    }
    return result;
}

} // namespace detail

// Generic Ref -> T conversion used by higher-level adapters. It only models
// reading a value for the duration of the current operation; callers decide
// whether a conversion is valid for parameters, properties, or script values.
template<class T>
struct Conversion {
    static ConversionRank match(const Ref& ref) {
        if (!ref) {
            return ConversionRank::None;
        }
        return ref.type_id() == fei::type_id<T>() ? ConversionRank::Exact :
                                                    ConversionRank::None;
    }

    static T get(const Ref& ref) { return T(ref.get_const<T>()); }
};

// Enums accept their exact reflected type and integer values. Integer enum
// values are weak conversions because overload resolution should prefer an
// exact enum argument when one is available.
template<class E>
    requires std::is_enum_v<E>
struct Conversion<E> {
    static ConversionRank match(const Ref& ref) {
        if (!ref) {
            return ConversionRank::None;
        }
        if (ref.type_id() == fei::type_id<E>()) {
            return ConversionRank::Exact;
        }
        if (ref.type_id() == fei::type_id<int>()) {
            return ConversionRank::Weak;
        }
        return ConversionRank::None;
    }

    static E get(const Ref& ref) {
        if (ref.type_id() == fei::type_id<int>()) {
            return static_cast<E>(ref.get_const<int>());
        }
        return ref.get_const<E>();
    }
};

// Arithmetic conversion follows the conservative policy above: exact matches
// win, widening-like conversions are weak, and narrowing is rejected.
template<class N>
    requires(std::is_arithmetic_v<N> && !std::same_as<N, bool>)
struct Conversion<N> {
    static ConversionRank match(const Ref& ref) {
        if (!ref) {
            return ConversionRank::None;
        }
        if (ref.type_id() == fei::type_id<N>()) {
            return ConversionRank::Exact;
        }
        if (detail::matches_numeric_source<N>(ref)) {
            return ConversionRank::Weak;
        }
        return ConversionRank::None;
    }

    static N get(const Ref& ref) { return N(ref.to_number<N>()); }
};

// string_view is a borrowed view. The returned view is only safe while the
// source Ref remains alive and unchanged, which is suitable for immediate
// reflected calls but not for storing into long-lived reflected state.
template<>
struct Conversion<std::string_view> {
    static ConversionRank match(const Ref& ref) {
        if (!ref) {
            return ConversionRank::None;
        }
        if (ref.type_id() == fei::type_id<std::string_view>()) {
            return ConversionRank::Exact;
        }
        if (ref.type_id() == fei::type_id<std::string>()) {
            return ConversionRank::Weak;
        }
        return ConversionRank::None;
    }

    static std::string_view get(const Ref& ref) {
        if (ref.type_id() == fei::type_id<std::string>()) {
            return std::string_view(ref.get_const<std::string>());
        }
        return std::string_view(ref.get_const<std::string_view>());
    }
};

} // namespace fei
