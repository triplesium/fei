#pragma once
#include "refl/type.hpp"

#include <concepts>
#include <unordered_set>

namespace fei {

template<typename T>
struct SystemSet;

struct SystemSetDependencies {
    std::unordered_set<TypeId> before;
    std::unordered_set<TypeId> after;
    std::unordered_set<TypeId> in_sets;
};

struct SystemSetConfig {
    SystemSetDependencies dependencies;
    TypeId set_id;

    template<typename T>
        requires std::derived_from<T, SystemSet<T>>
    SystemSetConfig& before() {
        dependencies.before.insert(type_id<T>());
        return *this;
    }
    template<typename T>
        requires std::derived_from<T, SystemSet<T>>
    SystemSetConfig& after() {
        dependencies.after.insert(type_id<T>());
        return *this;
    }
    template<typename T>
        requires std::derived_from<T, SystemSet<T>>
    SystemSetConfig& in_set() {
        dependencies.in_sets.insert(type_id<T>());
        return *this;
    }
};

template<typename T>
struct SystemSet {
    template<typename U>
        requires std::derived_from<U, SystemSet<U>>
    SystemSetConfig before() {
        return SystemSetConfig {
            .dependencies = SystemSetDependencies {.before = {type_id<U>()}},
            .set_id = type_id<T>()
        };
    }

    template<typename U>
        requires std::derived_from<U, SystemSet<U>>
    SystemSetConfig after() {
        return SystemSetConfig {
            .dependencies = SystemSetDependencies {.after = {type_id<U>()}},
            .set_id = type_id<T>()
        };
    }

    template<typename U>
        requires std::derived_from<U, SystemSet<U>>
    SystemSetConfig in_set() {
        return SystemSetConfig {
            .dependencies = SystemSetDependencies {.in_sets = {type_id<U>()}},
            .set_id = type_id<T>()
        };
    }
};

} // namespace fei
