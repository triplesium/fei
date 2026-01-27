#pragma once
#include "ecs/fwd.hpp"
#include "ecs/system.hpp"
#include "ecs/system_set.hpp"

#include <concepts>
#include <memory>
#include <unordered_set>
#include <utility>
#include <vector>

namespace fei {

struct SystemDependencies {
    // These are hashes of systems
    std::unordered_set<std::size_t> before;
    std::unordered_set<std::size_t> after;
    std::unordered_set<TypeId> in_sets;
};

struct SystemConfig {
    SystemDependencies dependencies;
    std::unique_ptr<System> system;
    SystemId id;
    static SystemId next_id;

    SystemConfig(std::unique_ptr<System> sys) :
        system(std::move(sys)), id(next_id++) {}
    template<IntoSystem F>
    SystemConfig(F func) :
        SystemConfig(std::make_unique<FunctionSystem<F>>(func)) {}

    SystemConfig(const SystemConfig&) = delete;
    SystemConfig& operator=(const SystemConfig&) = delete;
    SystemConfig(SystemConfig&&) noexcept = default;
    SystemConfig& operator=(SystemConfig&&) noexcept = default;

    SystemConfig&& before(size_t hash) && {
        dependencies.before.insert(hash);
        return std::move(*this);
    }
    SystemConfig&& after(size_t hash) && {
        dependencies.after.insert(hash);
        return std::move(*this);
    }
    SystemConfig&& in_set(TypeId set_id) && {
        dependencies.in_sets.insert(set_id);
        return std::move(*this);
    }
};

struct SystemConfigs {
    std::vector<SystemConfig> systems;
    SystemConfigs(SystemConfig config) { systems.push_back(std::move(config)); }
    SystemConfigs(std::vector<SystemConfig>&& configs) :
        systems(std::move(configs)) {}
    SystemConfigs(IntoSystem auto&&... configs) {
        (systems.push_back(SystemConfig(std::forward<decltype(configs)>(configs)
         )),
         ...);
    }
};

struct SystemBeforeTag {
    std::size_t hash;
};
inline SystemBeforeTag before(HashableSystem auto&& system) {
    return {.hash = hash_system(system)};
}
inline SystemConfig operator|(IntoSystem auto&& system, SystemBeforeTag tag) {
    return SystemConfig(system).before(tag.hash);
}
inline SystemConfig operator|(SystemConfig&& config, SystemBeforeTag tag) {
    return std::move(config).before(tag.hash);
}

struct SystemAfterTag {
    std::size_t hash;
};
inline SystemAfterTag after(HashableSystem auto&& system) {
    return {.hash = hash_system(system)};
}
inline SystemConfig operator|(IntoSystem auto&& system, SystemAfterTag tag) {
    return SystemConfig(system).after(tag.hash);
}
inline SystemConfig operator|(SystemConfig&& config, SystemAfterTag tag) {
    return std::move(config).after(tag.hash);
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
inline SystemConfig operator|(SystemConfig&& config, SystemInSetTag tag) {
    return std::move(config).in_set(tag.set_id);
}

inline SystemConfigs all(std::convertible_to<SystemConfig> auto&&... configs) {
    std::vector<SystemConfig> systems;
    (systems.push_back(std::forward<SystemConfig>(configs)), ...);
    return SystemConfigs {std::move(systems)};
}

inline SystemConfigs chain(std::convertible_to<SystemConfig> auto&&... configs
) {
    std::vector<SystemConfig> systems;
    (systems.push_back(std::forward<SystemConfig>(configs)), ...);
    for (int i = 1; i < systems.size(); ++i) {
        systems[i - 1].dependencies.before.insert(systems[i].system->hash());
    }
    return SystemConfigs {std::move(systems)};
}

inline SystemConfigs operator|(SystemConfigs&& config, SystemBeforeTag tag) {
    for (auto& sys_config : config.systems) {
        sys_config.dependencies.before.insert(tag.hash);
    }
    return std::move(config);
}

inline SystemConfigs operator|(SystemConfigs&& config, SystemAfterTag tag) {
    for (auto& sys_config : config.systems) {
        sys_config.dependencies.after.insert(tag.hash);
    }
    return std::move(config);
}

inline SystemConfigs operator|(SystemConfigs&& config, SystemInSetTag tag) {
    for (auto& sys_config : config.systems) {
        sys_config.dependencies.in_sets.insert(tag.set_id);
    }
    return std::move(config);
}

} // namespace fei
