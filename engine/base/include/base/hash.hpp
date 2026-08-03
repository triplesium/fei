#pragma once
#include "base/macro.hpp"

#include <concepts>
#include <functional>
#include <ranges>
#include <type_traits>

namespace fei {

template<typename T>
concept is_std_hashable = requires(const T& object) {
    { std::hash<T> {}(object) } -> std::convertible_to<std::size_t>;
};

template<typename T>
struct is_hash_combinable_impl; // NOLINT(readability-identifier-naming)

template<typename T>
concept is_hash_combinable = is_hash_combinable_impl<T>::value;

template<typename T>
struct is_hash_combinable_impl // NOLINT(readability-identifier-naming)
    : std::bool_constant<is_std_hashable<T>> {};

template<std::ranges::range T>
struct is_hash_combinable_impl<T> // NOLINT(readability-identifier-naming)
    : std::bool_constant<
          is_std_hashable<T> ||
          is_hash_combinable<std::ranges::range_value_t<T>>> {};

template<is_hash_combinable T>
inline std::size_t hash_value(const T& value);

template<is_hash_combinable T>
inline void hash_combine(std::size_t& seed, const T& value) {
    seed ^= hash_value(value) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
}

template<is_hash_combinable T>
inline std::size_t hash_value(const T& value) {
    if constexpr (is_std_hashable<T>) {
        return std::hash<T> {}(value);
    } else {
        std::size_t seed = 0;
        for (const auto& element : value) {
            hash_combine(seed, element);
        }
        return seed;
    }
}

template<is_hash_combinable... Ts>
std::size_t hash_combine_all(const Ts&... args) {
    std::size_t seed = 0;
    (hash_combine(seed, args), ...);
    return seed;
}

} // namespace fei

#define FEI_HASH_COMBINE_MEMBER(x) (fei::hash_combine(seed, obj.x), 0)

#define MAKE_STD_HASHABLE(CLASS, ...)                             \
    template<>                                                    \
    struct std::hash<CLASS> {                                     \
        std::size_t operator()(const CLASS& obj) const {          \
            std::size_t seed = 0;                                 \
            [[maybe_unused]] int unused[] = {                     \
                0,                                                \
                FEI_FOREACH(FEI_HASH_COMBINE_MEMBER, __VA_ARGS__) \
            };                                                    \
            return seed;                                          \
        }                                                         \
    };
