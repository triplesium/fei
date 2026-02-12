#pragma once
#include "base/macro.hpp"

#include <functional>
#include <ranges>

namespace fei {

template<class T>
inline void hash_combine(std::size_t& seed, const T& v) {
    std::hash<T> hasher;
    seed ^= hasher(v) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
}

template<typename T>
concept is_std_hashable = requires(const T& object) {
    { std::hash<T> {}(object) } -> std::convertible_to<std::size_t>;
};

template<typename T>
concept is_iterable = requires(const T& object) {
    object.begin();
    object.end();
};

template<is_std_hashable... Ts>
std::size_t hash_combine_all(const Ts&... args) {
    std::size_t seed = 0;
    (hash_combine(seed, args), ...);
    return seed;
}

} // namespace fei

namespace std {

template<typename T>
    requires ranges::range<T> &&
             fei::is_std_hashable<std::ranges::range_value_t<T>>
struct hash<T> {
    std::size_t operator()(const T& iterable) const {
        std::size_t seed = 0;
        for (const auto& element : iterable) {
            fei::hash_combine(seed, element);
        }
        return seed;
    }
};
static_assert(fei::is_std_hashable<std::vector<int>>);

} // namespace std

#define EXPAND_WITH_OBJ_PREFIX(...) FEI_FOREACH(FEI_ADD_OBJ_PREFIX, __VA_ARGS__)

#define FEI_ADD_OBJ_PREFIX(x) obj.x

#define MAKE_STD_HASHABLE(CLASS, ...)                                          \
    template<>                                                                 \
    struct std::hash<CLASS> {                                                  \
        std::size_t operator()(const CLASS& obj) const {                       \
            return fei::hash_combine_all(EXPAND_WITH_OBJ_PREFIX(__VA_ARGS__)); \
        }                                                                      \
    };
