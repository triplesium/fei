#pragma once
#include "ecs/fwd.hpp"
#include "ecs/system.hpp"
#include "ecs/system_profile.hpp"
#include "ecs/system_set.hpp"

#include <concepts>
#include <memory>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <vector>

namespace fei {

template<typename T>
concept NamedIntoSystem = NamedSystemWrapper<T> &&
                          IntoSystem<typename std::remove_cvref_t<T>::FuncType>;

template<typename T>
concept IntoSystemConfig = IntoSystem<T> || NamedIntoSystem<T>;

struct SystemDependencies {
    std::unordered_set<SystemId> before;
    std::unordered_set<SystemId> after;
    std::unordered_set<TypeId> in_sets;
};

struct SystemConfig {
    SystemDependencies dependencies;
    std::unique_ptr<System> system;
    std::vector<std::unique_ptr<Condition>> conditions;
    SystemProfileInfo profile;
    bool main_thread_only {false};
    SystemId id;
    static SystemId next_id;

    explicit SystemConfig(std::unique_ptr<System> sys) :
        system(std::move(sys)), id(next_id++) {}
    template<IntoSystem F>
    explicit SystemConfig(F func) :
        SystemConfig(std::make_unique<FunctionSystem<F>>(func)) {}
    template<NamedIntoSystem F>
    explicit SystemConfig(F&& named) :
        SystemConfig(
            std::make_unique<
                FunctionSystem<typename std::remove_cvref_t<F>::FuncType>>(
                std::forward<F>(named).func
            )
        ) {
        profile =
            SystemProfileInfo::from_source_location(named.name, named.location);
    }

    SystemConfig(const SystemConfig&) = delete;
    SystemConfig& operator=(const SystemConfig&) = delete;
    SystemConfig(SystemConfig&&) noexcept = default;
    SystemConfig& operator=(SystemConfig&&) noexcept = default;

    SystemConfig& before(const SystemConfig& target) & {
        dependencies.before.insert(target.id);
        return *this;
    }
    SystemConfig&& before(const SystemConfig& target) && {
        before(target);
        return std::move(*this);
    }
    SystemConfig& after(const SystemConfig& target) & {
        dependencies.after.insert(target.id);
        return *this;
    }
    SystemConfig&& after(const SystemConfig& target) && {
        after(target);
        return std::move(*this);
    }
    SystemConfig&& in_set(TypeId set_id) && {
        dependencies.in_sets.insert(set_id);
        return std::move(*this);
    }
    SystemConfig&& main_thread() && {
        main_thread_only = true;
        return std::move(*this);
    }
    template<IntoCondition F>
    void add_condition(F&& condition) {
        using ConditionType = std::decay_t<F>;
        conditions.push_back(
            std::make_unique<FunctionCondition<ConditionType>>(
                std::forward<F>(condition)
            )
        );
    }
    template<IntoCondition F>
    SystemConfig&& run_if(F&& condition) && {
        add_condition(std::forward<F>(condition));
        return std::move(*this);
    }
};

struct SystemConfigs {
    std::vector<SystemConfig> systems;
    SystemConfigs(SystemConfig config) { systems.push_back(std::move(config)); }
    SystemConfigs(std::vector<SystemConfig>&& configs) :
        systems(std::move(configs)) {}
    SystemConfigs(IntoSystemConfig auto&&... configs) {
        (systems.push_back(
             SystemConfig(std::forward<decltype(configs)>(configs))
         ),
         ...);
    }

    SystemConfigs(const SystemConfigs&) = delete;
    SystemConfigs& operator=(const SystemConfigs&) = delete;
    SystemConfigs(SystemConfigs&&) noexcept = default;
    SystemConfigs& operator=(SystemConfigs&&) noexcept = default;
};

struct SystemBeforeTag {
    SystemId id;
};
inline SystemBeforeTag before(const SystemConfig& system) {
    return {.id = system.id};
}
inline SystemConfig operator|(IntoSystem auto&& system, SystemBeforeTag tag) {
    auto config = SystemConfig(system);
    config.dependencies.before.insert(tag.id);
    return config;
}
inline SystemConfig
operator|(NamedIntoSystem auto&& system, SystemBeforeTag tag) {
    auto config = SystemConfig(std::forward<decltype(system)>(system));
    config.dependencies.before.insert(tag.id);
    return config;
}
inline SystemConfig operator|(SystemConfig&& config, SystemBeforeTag tag) {
    config.dependencies.before.insert(tag.id);
    return std::move(config);
}

struct SystemAfterTag {
    SystemId id;
};
inline SystemAfterTag after(const SystemConfig& system) {
    return {.id = system.id};
}
inline SystemConfig operator|(IntoSystem auto&& system, SystemAfterTag tag) {
    auto config = SystemConfig(system);
    config.dependencies.after.insert(tag.id);
    return config;
}
inline SystemConfig
operator|(NamedIntoSystem auto&& system, SystemAfterTag tag) {
    auto config = SystemConfig(std::forward<decltype(system)>(system));
    config.dependencies.after.insert(tag.id);
    return config;
}
inline SystemConfig operator|(SystemConfig&& config, SystemAfterTag tag) {
    config.dependencies.after.insert(tag.id);
    return std::move(config);
}

struct SystemInSetTag {
    TypeId set_id;
};
template<typename T>
    requires std::derived_from<T, SystemSet<T>>
inline SystemInSetTag in_set() {
    return {type_id<T>()};
}
inline SystemConfig operator|(IntoSystem auto&& system, SystemInSetTag tag) {
    return SystemConfig(system).in_set(tag.set_id);
}
inline SystemConfig
operator|(NamedIntoSystem auto&& system, SystemInSetTag tag) {
    return SystemConfig(std::forward<decltype(system)>(system))
        .in_set(tag.set_id);
}
inline SystemConfig operator|(SystemConfig&& config, SystemInSetTag tag) {
    return std::move(config).in_set(tag.set_id);
}

struct SystemMainThreadTag {};
inline SystemMainThreadTag main_thread() {
    return {};
}
inline SystemConfig
operator|(IntoSystem auto&& system, SystemMainThreadTag /*tag*/) {
    return SystemConfig(system).main_thread();
}
inline SystemConfig
operator|(NamedIntoSystem auto&& system, SystemMainThreadTag /*tag*/) {
    return SystemConfig(std::forward<decltype(system)>(system)).main_thread();
}
inline SystemConfig
operator|(SystemConfig&& config, SystemMainThreadTag /*tag*/) {
    return std::move(config).main_thread();
}

template<IntoCondition F>
struct SystemRunIfTag {
    F condition;
};

template<IntoCondition F>
inline SystemRunIfTag<std::decay_t<F>> run_if(F&& condition) {
    return {std::forward<F>(condition)};
}

template<IntoCondition C>
inline SystemConfig operator|(IntoSystem auto&& system, SystemRunIfTag<C> tag) {
    return SystemConfig(std::forward<decltype(system)>(system))
        .run_if(std::move(tag.condition));
}

template<IntoCondition C>
inline SystemConfig
operator|(NamedIntoSystem auto&& system, SystemRunIfTag<C> tag) {
    return SystemConfig(std::forward<decltype(system)>(system))
        .run_if(std::move(tag.condition));
}

template<IntoCondition C>
inline SystemConfig operator|(SystemConfig&& config, SystemRunIfTag<C> tag) {
    return std::move(config).run_if(std::move(tag.condition));
}

inline SystemConfigs all(std::convertible_to<SystemConfigs> auto&&... configs) {
    std::vector<SystemConfig> systems;
    (
        [config = SystemConfigs(std::forward<decltype(configs)>(configs)),
         &systems]() mutable {
            systems.insert(
                systems.end(),
                std::make_move_iterator(config.systems.begin()),
                std::make_move_iterator(config.systems.end())
            );
        }(),
        ...
    );
    return SystemConfigs {std::move(systems)};
}

inline SystemConfigs
chain(std::convertible_to<SystemConfigs> auto&&... configs) {
    std::vector<SystemConfigs> system_configs;
    system_configs.reserve(sizeof...(configs));
    (system_configs.push_back(
         SystemConfigs(std::forward<decltype(configs)>(configs))
     ),
     ...);

    for (std::size_t i = 0; i < system_configs.size() - 1; ++i) {
        auto& former = system_configs[i];
        auto& latter = system_configs[i + 1];
        for (auto& former_config : former.systems) {
            for (auto& latter_config : latter.systems) {
                former_config.dependencies.before.insert(latter_config.id);
            }
        }
    }
    std::vector<SystemConfig> systems;
    for (auto& config : system_configs) {
        systems.insert(
            systems.end(),
            std::make_move_iterator(config.systems.begin()),
            std::make_move_iterator(config.systems.end())
        );
    }
    return SystemConfigs {std::move(systems)};
}

inline SystemConfigs operator|(SystemConfigs&& config, SystemBeforeTag tag) {
    for (auto& sys_config : config.systems) {
        sys_config.dependencies.before.insert(tag.id);
    }
    return std::move(config);
}

inline SystemConfigs operator|(SystemConfigs&& config, SystemAfterTag tag) {
    for (auto& sys_config : config.systems) {
        sys_config.dependencies.after.insert(tag.id);
    }
    return std::move(config);
}

inline SystemConfigs operator|(SystemConfigs&& config, SystemInSetTag tag) {
    for (auto& sys_config : config.systems) {
        sys_config.dependencies.in_sets.insert(tag.set_id);
    }
    return std::move(config);
}

inline SystemConfigs
operator|(SystemConfigs&& config, SystemMainThreadTag /*tag*/) {
    for (auto& sys_config : config.systems) {
        sys_config.main_thread_only = true;
    }
    return std::move(config);
}

template<IntoCondition C>
    requires std::copy_constructible<C>
inline SystemConfigs operator|(SystemConfigs&& config, SystemRunIfTag<C> tag) {
    for (auto& sys_config : config.systems) {
        sys_config.add_condition(tag.condition);
    }
    return std::move(config);
}

} // namespace fei
