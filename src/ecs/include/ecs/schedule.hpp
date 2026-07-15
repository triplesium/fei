#pragma once
#include "base/log.hpp"
#include "base/optional.hpp"
#include "base/thread_pool.hpp"
#include "ecs/fwd.hpp"
#include "ecs/system.hpp"
#include "ecs/system_config.hpp"
#include "ecs/system_set.hpp"
#include "refl/type.hpp"

#include <concepts>
#include <cstddef>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace fei {

struct SystemScheduleDebugInfo {
    SystemId id {0};
    std::string name;
    std::vector<SystemId> dependencies;
    std::size_t topological_index {0};
    std::size_t batch_index {0};
};

struct ScheduleDebugInfo {
    ScheduleId id {0};
    std::vector<SystemScheduleDebugInfo> systems;
    std::vector<std::vector<SystemId>> batches;
};

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
    void clear() {
        m_edges.clear();
        m_sorted_nodes.clear();
    }
    void sort();
    const std::vector<SystemId>& sorted_nodes() const { return m_sorted_nodes; }
    const std::unordered_map<SystemId, std::vector<SystemId>>& edges() const {
        return m_edges;
    }
};

class Schedule {
  private:
    std::unordered_map<TypeId, SystemSetConfig> m_system_set_configs;
    std::unordered_map<TypeId, std::vector<SystemId>> m_system_set_members;
    std::unordered_map<SystemId, SystemConfig> m_systems;
    ScheduleGraph m_graph;
    std::vector<std::vector<SystemId>> m_execution_batches;
    bool m_dirty {true};

  public:
    Schedule() = default;
    ~Schedule() = default;

    Schedule(const Schedule&) = delete;
    Schedule& operator=(const Schedule&) = delete;
    Schedule(Schedule&&) noexcept = default;
    Schedule& operator=(Schedule&&) noexcept = default;

    SystemId add_system(SystemConfig config);
    std::vector<SystemId> add_systems(SystemConfigs configs);

    std::vector<SystemId>
    add_systems(std::convertible_to<SystemConfigs> auto&&... configs) {
        std::vector<SystemId> ids;
        (
            [this,
             config = SystemConfigs(std::forward<decltype(configs)>(configs)),
             &ids]() mutable {
                auto added_ids = add_systems(std::move(config));
                ids.insert(ids.end(), added_ids.begin(), added_ids.end());
            }(),
            ...
        );
        return ids;
    }

    bool remove_system(SystemId id);
    bool replace_system(SystemId id, SystemConfig config);

    void sort_systems() { rebuild_execution_plan(); }

    void configure_set(SystemSetConfigs config) {
        for (auto& set_config : config.sets) {
            auto set_id = set_config.set_id;
            auto [it, inserted] =
                m_system_set_configs.try_emplace(set_id, std::move(set_config));
            if (!inserted) {
                it->second.merge(set_config);
            }
        }
        m_dirty = true;
    }

    void
    configure_sets(std::convertible_to<SystemSetConfigs> auto&&... configs) {
        (configure_set(std::forward<decltype(configs)>(configs)), ...);
    }

    void run_systems(World& world);
    void run_systems(ScheduleId schedule, World& world);
    void run_systems(World& world, ThreadPool& thread_pool);
    void
    run_systems(ScheduleId schedule, World& world, ThreadPool& thread_pool);

    const std::vector<std::vector<SystemId>>& execution_batches() const {
        return m_execution_batches;
    }

    ScheduleDebugInfo debug_info(ScheduleId schedule);

  private:
    void ensure_execution_plan();
    void rebuild_execution_plan();
    void resolve_system_profiles();
    void build_execution_batches();

    void resolve_dependencies() {
        // Build the system set map.
        for (auto& [id, config] : m_systems) {
            for (auto set_id : config.dependencies.in_sets) {
                m_system_set_members[set_id].push_back(id);
                if (!m_system_set_configs.contains(set_id)) {
                    m_system_set_configs.emplace(set_id, SystemSetConfig {});
                }
            }
        }
        for (auto& [set_id, set_config] : m_system_set_configs) {
            if (!m_system_set_members.contains(set_id)) {
                m_system_set_members[set_id] = {};
            }
        }
    }

    void build_graph() {
        for (auto& [id, config] : m_systems) {
            m_graph.add_node(id);
        }
        for (auto& [id, config] : m_systems) {
            for (auto before_id : config.dependencies.before) {
                if (!m_systems.contains(before_id)) {
                    fei::fatal(
                        "System {} 'before' dependency target {} is not "
                        "registered",
                        id,
                        before_id
                    );
                }
                m_graph.add_edge(id, before_id);
            }
            for (auto after_id : config.dependencies.after) {
                if (!m_systems.contains(after_id)) {
                    fei::fatal(
                        "System {} 'after' dependency target {} is not "
                        "registered",
                        id,
                        after_id
                    );
                }
                m_graph.add_edge(after_id, id);
            }
            for (auto set_id : config.dependencies.in_sets) {
                if (!m_system_set_configs.contains(set_id)) {
                    fei::fatal("SystemSet dependency not found");
                }
                auto& set_config = m_system_set_configs[set_id];
                for (auto before_set : set_config.dependencies.before) {
                    if (!m_system_set_members.contains(before_set)) {
                        fei::fatal("SystemSet dependency not found");
                    }
                    for (auto sys_id : m_system_set_members[before_set]) {
                        m_graph.add_edge(id, sys_id);
                    }
                }
                for (auto after_set : set_config.dependencies.after) {
                    if (!m_system_set_members.contains(after_set)) {
                        fei::fatal("SystemSet dependency not found");
                    }
                    for (auto sys_id : m_system_set_members[after_set]) {
                        m_graph.add_edge(sys_id, id);
                    }
                }
            }
        }
    }
};

class Schedules {
  private:
    std::unordered_map<ScheduleId, Schedule> m_schedules;
    std::unique_ptr<ThreadPool> m_thread_pool;

  public:
    Schedules();

    Schedules(const Schedules&) = delete;
    Schedules& operator=(const Schedules&) = delete;
    Schedules(Schedules&&) noexcept = default;
    Schedules& operator=(Schedules&&) noexcept = default;

    SystemHandle add_system(ScheduleId schedule, SystemConfig config);

    std::vector<SystemHandle> add_systems(
        ScheduleId schedule,
        std::convertible_to<SystemConfigs> auto&&... configs
    ) {
        auto& target = m_schedules[schedule];
        std::vector<SystemHandle> handles;
        (
            [schedule,
             &target,
             config = SystemConfigs(std::forward<decltype(configs)>(configs)),
             &handles]() mutable {
                auto ids = target.add_systems(std::move(config));
                handles.reserve(handles.size() + ids.size());
                for (auto id : ids) {
                    handles.push_back(
                        SystemHandle {.schedule = schedule, .id = id}
                    );
                }
            }(),
            ...
        );
        return handles;
    }

    bool remove_system(SystemHandle handle);
    bool replace_system(SystemHandle handle, SystemConfig config);

    void configure_sets(
        ScheduleId schedule,
        std::convertible_to<SystemSetConfigs> auto&&... configs
    ) {
        m_schedules[schedule].configure_sets(
            std::forward<decltype(configs)>(configs)...
        );
    }

    void sort_systems() {
        for (auto& [_, schedule] : m_schedules) {
            schedule.sort_systems();
        }
    }

    void set_worker_threads(std::size_t thread_count);
    std::size_t worker_threads() const;

    void run_systems(ScheduleId schedule, World& world);
    Optional<ScheduleDebugInfo> debug_info(ScheduleId schedule);
};

} // namespace fei
