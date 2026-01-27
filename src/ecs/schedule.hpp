#pragma once
#include "base/log.hpp"
#include "ecs/fwd.hpp"
#include "ecs/system.hpp"
#include "ecs/system_config.hpp"
#include "ecs/system_set.hpp"
#include "refl/type.hpp"

#include <concepts>
#include <cstddef>
#include <memory>
#include <unordered_map>
#include <vector>

namespace fei {

using ScheduleId = std::size_t;

class ScheduleGraph {
  private:
    std::unordered_map<SystemId, std::vector<SystemId>> m_edges;
    std::vector<SystemId> m_sorted_nodes;

  public:
    ScheduleGraph() = default;
    ~ScheduleGraph() = default;

    void add_node(SystemId node) {
        m_edges.try_emplace(node, std::vector<SystemId> {});
    }
    void add_edge(SystemId from, SystemId to) { m_edges[from].push_back(to); }
    void sort();
    const std::vector<SystemId>& sorted_nodes() const { return m_sorted_nodes; }
};

class Schedule {
  private:
    std::unordered_map<std::size_t, SystemId> m_system_hash_map;
    std::unordered_map<TypeId, SystemSetConfig> m_system_set_configs;
    std::unordered_map<TypeId, std::vector<SystemId>> m_system_set_hash_map;
    std::unordered_map<SystemId, SystemConfig> m_systems;
    ScheduleGraph m_graph;

  public:
    Schedule() = default;
    ~Schedule() = default;

    Schedule(const Schedule&) = delete;
    Schedule& operator=(const Schedule&) = delete;
    Schedule(Schedule&&) noexcept = default;
    Schedule& operator=(Schedule&&) noexcept = default;

    void add_system(SystemConfigs configs) {
        for (auto& config : configs.systems) {
            m_systems.emplace(config.id, std::move(config));
        }
    }

    void add_systems(std::convertible_to<SystemConfigs> auto&&... configs) {
        (add_system(std::move(configs)), ...);
    }

    void sort_systems() {
        resolve_dependencies();
        build_graph();
        m_graph.sort();
    }

    void configure_set(SystemSetConfig config) {
        m_system_set_configs[config.set_id] = std::move(config);
    }

    void configure_sets(std::convertible_to<SystemSetConfig> auto&&... configs
    ) {
        (configure_set(std::forward<SystemSetConfig>(configs)), ...);
    }

    void run_systems(World& world);

  private:
    void resolve_dependencies() {
        // Build the system hash map & system set hash map
        for (auto& [id, config] : m_systems) {
            if (config.system->hashable()) {
                m_system_hash_map[config.system->hash()] = id;
            }
            for (auto set_id : config.dependencies.in_sets) {
                m_system_set_hash_map[set_id].push_back(id);
                if (!m_system_set_configs.contains(set_id)) {
                    m_system_set_configs.emplace(set_id, SystemSetConfig {});
                }
            }
        }
        // Resolve in_sets
        for (auto& [id, config] : m_systems) {
            auto& deps = config.dependencies;
            for (auto set_id : deps.in_sets) {
                if (!m_system_set_configs.contains(set_id)) {
                    fei::fatal("SystemSet dependency not found");
                }
                auto& set_config = m_system_set_configs[set_id];
                for (auto before_set : set_config.dependencies.before) {
                    if (!m_system_set_hash_map.contains(before_set)) {
                        fei::fatal("SystemSet dependency not found");
                    }
                    for (auto sys_id : m_system_set_hash_map[before_set]) {
                        config.dependencies.before.insert(
                            m_systems.at(sys_id).system->hash()
                        );
                    }
                }
                for (auto after_set : set_config.dependencies.after) {
                    if (!m_system_set_hash_map.contains(after_set)) {
                        fei::fatal("SystemSet dependency not found");
                    }
                    for (auto sys_id : m_system_set_hash_map[after_set]) {
                        config.dependencies.after.insert(
                            m_systems.at(sys_id).system->hash()
                        );
                    }
                }
            }
        }
    }

    void build_graph() {
        for (auto& [id, config] : m_systems) {
            m_graph.add_node(id);
        }
        for (auto& [id, config] : m_systems) {
            for (auto before_hash : config.dependencies.before) {
                auto it = m_system_hash_map.find(before_hash);
                if (it != m_system_hash_map.end()) {
                    m_graph.add_edge(id, it->second);
                }
            }
            for (auto after_hash : config.dependencies.after) {
                auto it = m_system_hash_map.find(after_hash);
                if (it != m_system_hash_map.end()) {
                    m_graph.add_edge(it->second, id);
                }
            }
        }
    }
};

class SystemScheduler {
  private:
    std::unordered_map<ScheduleId, Schedule> m_schedules;

  public:
    SystemScheduler() = default;

    SystemScheduler(const SystemScheduler&) = delete;
    SystemScheduler& operator=(const SystemScheduler&) = delete;
    SystemScheduler(SystemScheduler&&) noexcept = default;
    SystemScheduler& operator=(SystemScheduler&&) noexcept = default;

    void add_system(ScheduleId schedule, SystemConfig&& config) {
        m_schedules[schedule].add_system(std::move(config));
    }

    void run_systems(ScheduleId schedule, World& world);
};

} // namespace fei
