#pragma once
#include "refl/type.hpp"

#include <concepts>
#include <unordered_set>
#include <vector>

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
    SystemSetConfig& before(TypeId set_id) {
        dependencies.before.insert(set_id);
        return *this;
    }
    template<typename T>
        requires std::derived_from<T, SystemSet<T>>
    SystemSetConfig& after() {
        dependencies.after.insert(type_id<T>());
        return *this;
    }
    SystemSetConfig& after(TypeId set_id) {
        dependencies.after.insert(set_id);
        return *this;
    }
    template<typename T>
        requires std::derived_from<T, SystemSet<T>>
    SystemSetConfig& in_set() {
        dependencies.in_sets.insert(type_id<T>());
        return *this;
    }
    SystemSetConfig& in_set(TypeId set_id) {
        dependencies.in_sets.insert(set_id);
        return *this;
    }
};

struct SystemSetConfigs {
    std::vector<SystemSetConfig> sets;

    SystemSetConfigs(SystemSetConfig config) : sets {std::move(config)} {}
    SystemSetConfigs(std::vector<SystemSetConfig>&& configs) :
        sets {std::move(configs)} {}

    template<typename T>
        requires std::derived_from<T, SystemSet<T>>
    SystemSetConfigs(T&& set) {
        sets.push_back(SystemSetConfig {
            .dependencies = SystemSetDependencies {},
            .set_id = type_id<T>()
        });
    }

    template<typename T>
        requires std::derived_from<T, SystemSet<T>>
    SystemSetConfigs& before() {
        for (auto& set : sets) {
            set.before<T>();
        }
        return *this;
    }
    SystemSetConfigs& before(TypeId set_id) {
        for (auto& set : sets) {
            set.before(set_id);
        }
        return *this;
    }
    template<typename T>
        requires std::derived_from<T, SystemSet<T>>
    SystemSetConfigs& after() {
        for (auto& set : sets) {
            set.after<T>();
        }
        return *this;
    }
    SystemSetConfigs& after(TypeId set_id) {
        for (auto& set : sets) {
            set.after(set_id);
        }
        return *this;
    }
    template<typename T>
        requires std::derived_from<T, SystemSet<T>>
    SystemSetConfigs& in_set() {
        for (auto& set : sets) {
            set.in_set<T>();
        }
        return *this;
    }
    SystemSetConfigs& in_set(TypeId set_id) {
        for (auto& set : sets) {
            set.in_set(set_id);
        }
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

SystemSetConfigs all(std::convertible_to<SystemSetConfigs> auto&&... sets) {
    std::vector<SystemSetConfig> system_set_configs;
    (
        [configs = SystemSetConfigs(std::forward<decltype(sets)>(sets)),
         &system_set_configs]() {
            system_set_configs.insert(
                system_set_configs.end(),
                configs.sets.begin(),
                configs.sets.end()
            );
        }(),
        ...
    );
    return SystemSetConfigs {std::move(system_set_configs)};
}

SystemSetConfigs chain(std::convertible_to<SystemSetConfigs> auto&&... sets) {
    std::vector<SystemSetConfigs> configs {
        SystemSetConfigs(std::forward<decltype(sets)>(sets))...
    };
    for (std::size_t i = 1; i < configs.size(); ++i) {
        auto& former = configs[i - 1];
        auto& latter = configs[i];
        for (auto& former_set : former.sets) {
            for (auto& latter_set : latter.sets) {
                former_set.before(latter_set.set_id);
            }
        }
    }
    std::vector<SystemSetConfig> system_set_configs;
    for (auto& config : configs) {
        system_set_configs.insert(
            system_set_configs.end(),
            config.sets.begin(),
            config.sets.end()
        );
    }
    return SystemSetConfigs {std::move(system_set_configs)};
}

} // namespace fei
