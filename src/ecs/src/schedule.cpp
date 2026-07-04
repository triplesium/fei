#include "ecs/schedule.hpp"

#include "ecs/commands.hpp"
#include "ecs/fwd.hpp"
#include "ecs/system.hpp"
#include "ecs/world.hpp"

#include <exception>
#include <future>
#include <queue>
#include <unordered_map>
#include <unordered_set>

#if defined(FEI_ENABLE_TRACY) || defined(FEI_ENABLE_PROFILE_SUMMARY)
#    include "ecs/system_profile.hpp"
#    include "profiling/profiling.hpp"

#    include <string>
#endif

namespace fei {
namespace {

#if defined(FEI_ENABLE_TRACY) || defined(FEI_ENABLE_PROFILE_SUMMARY)
void resolve_system_profile(SystemConfig& config) {
    if (!config.profile.named() && config.system->hashable()) {
        auto& registry = SystemProfileRegistry::instance();
        auto profile = registry.symbolize(config.system->hash());
        if (!profile) {
            profile = registry.find(config.system->hash());
        }
        if (profile && profile->named()) {
            config.profile = std::move(*profile);
        }
    }

    if (!config.profile.named()) {
        config.profile.name = "system#" + std::to_string(config.id);
    }
    if (config.profile.file.empty()) {
        config.profile.file = "<unknown>";
    }
    if (config.profile.function.empty()) {
        config.profile.function = config.profile.name;
    }
}
#endif

void run_profiled_system(
    ScheduleId schedule,
    SystemConfig& config,
    World& world
) {
#if defined(FEI_ENABLE_TRACY) || defined(FEI_ENABLE_PROFILE_SUMMARY)
    resolve_system_profile(config);
    FEI_PROFILE_SYSTEM_SCOPE(schedule, config.profile);
#endif
    config.system->run(world);
}

bool should_run(SystemConfig& config, World& world) {
    for (auto& condition : config.conditions) {
        if (!condition->run(world)) {
            return false;
        }
    }
    return true;
}

SystemAccess effective_access(const SystemConfig& config) {
    auto access = config.system->access();
    for (const auto& condition : config.conditions) {
        access.merge(condition->access());
    }
    access.main_thread_only =
        access.main_thread_only || config.main_thread_only;
    return access;
}

} // namespace

SystemId SystemConfig::next_id = 0;

Schedules::Schedules() : m_thread_pool(std::make_unique<ThreadPool>()) {}

SystemHandle Schedules::add_system(ScheduleId schedule, SystemConfig config) {
    return SystemHandle {
        .schedule = schedule,
        .id = m_schedules[schedule].add_system(std::move(config))
    };
}

bool Schedules::remove_system(SystemHandle handle) {
    auto it = m_schedules.find(handle.schedule);
    if (it == m_schedules.end()) {
        return false;
    }
    return it->second.remove_system(handle.id);
}

bool Schedules::replace_system(SystemHandle handle, SystemConfig config) {
    auto it = m_schedules.find(handle.schedule);
    if (it == m_schedules.end()) {
        return false;
    }
    return it->second.replace_system(handle.id, std::move(config));
}

void ScheduleGraph::sort() {
    m_sorted_nodes.clear();
    std::unordered_map<SystemId, int> in_degree;
    for (auto& [node, _] : m_edges) {
        in_degree[node] = 0;
    }
    for (const auto& [from, tos] : m_edges) {
        for (auto to : tos) {
            in_degree[to]++;
        }
    }
    std::queue<SystemId> q;
    for (auto& [node, _] : m_edges) {
        if (in_degree[node] == 0) {
            q.push(node);
        }
    }
    while (!q.empty()) {
        auto node = q.front();
        q.pop();
        m_sorted_nodes.push_back(node);
        for (auto to : m_edges[node]) {
            if (--in_degree[to] == 0) {
                q.push(to);
            }
        }
    }
    if (m_sorted_nodes.size() != m_edges.size()) {
        fei::fatal("Cycle detected in system dependencies");
    }
}

void Schedules::run_systems(ScheduleId schedule, World& world) {
    auto it = m_schedules.find(schedule);
    if (it != m_schedules.end()) {
        it->second.run_systems(schedule, world, *m_thread_pool);
    }
}

void Schedules::set_worker_threads(std::size_t thread_count) {
    m_thread_pool = std::make_unique<ThreadPool>(thread_count);
}

std::size_t Schedules::worker_threads() const {
    return m_thread_pool->thread_count();
}

SystemId Schedule::add_system(SystemConfig config) {
    auto id = config.id;
    auto [_, inserted] = m_systems.emplace(id, std::move(config));
    if (!inserted) {
        fei::fatal("System with id {} has already been added", id);
    }
    m_dirty = true;
    return id;
}

std::vector<SystemId> Schedule::add_systems(SystemConfigs configs) {
    std::vector<SystemId> ids;
    ids.reserve(configs.systems.size());
    for (auto& config : configs.systems) {
        ids.push_back(add_system(std::move(config)));
    }
    return ids;
}

bool Schedule::remove_system(SystemId id) {
    if (m_systems.erase(id) == 0) {
        return false;
    }
    m_dirty = true;
    return true;
}

bool Schedule::replace_system(SystemId id, SystemConfig config) {
    auto it = m_systems.find(id);
    if (it == m_systems.end()) {
        return false;
    }
    config.id = id;
    it->second = std::move(config);
    m_dirty = true;
    return true;
}

void Schedule::ensure_execution_plan() {
    if (m_dirty) {
        rebuild_execution_plan();
    }
}

void Schedule::rebuild_execution_plan() {
    m_system_hash_map.clear();
    m_system_set_hash_map.clear();
    m_graph.clear();
    m_execution_batches.clear();

    resolve_dependencies();
    resolve_system_profiles();
    build_graph();
    m_graph.sort();
    build_execution_batches();
    m_dirty = false;
}

void Schedule::run_systems(World& world) {
    run_systems(0, world);
}

void Schedule::run_systems(ScheduleId schedule, World& world) {
    ensure_execution_plan();

    for (const auto& batch : m_execution_batches) {
        for (auto system_id : batch) {
            auto it = m_systems.find(system_id);
            if (it != m_systems.end() && should_run(it->second, world)) {
                run_profiled_system(schedule, it->second, world);
            }
        }
        world.resource<CommandsQueue>().execute_after_batch(world);
    }
    world.resource<CommandsQueue>().execute_after_schedule(world);
}

void Schedule::run_systems(World& world, ThreadPool& thread_pool) {
    run_systems(0, world, thread_pool);
}

void Schedule::run_systems(
    ScheduleId schedule,
    World& world,
    ThreadPool& thread_pool
) {
    ensure_execution_plan();

    auto run_one = [this, schedule, &world](SystemId system_id) {
        auto it = m_systems.find(system_id);
        if (it != m_systems.end() && should_run(it->second, world)) {
            run_profiled_system(schedule, it->second, world);
        }
    };

    for (const auto& batch : m_execution_batches) {
        if (batch.size() == 1 || thread_pool.thread_count() == 1) {
            for (auto system_id : batch) {
                run_one(system_id);
            }
        } else {
            std::vector<std::future<void>> jobs;
            jobs.reserve(batch.size());
            for (auto system_id : batch) {
                jobs.push_back(thread_pool.submit([run_one, system_id]() {
                    run_one(system_id);
                }));
            }

            std::exception_ptr exception;
            for (auto& job : jobs) {
                try {
                    job.get();
                } catch (...) {
                    if (!exception) {
                        exception = std::current_exception();
                    }
                }
            }
            if (exception) {
                std::rethrow_exception(exception);
            }
        }
        world.resource<CommandsQueue>().execute_after_batch(world);
    }
    world.resource<CommandsQueue>().execute_after_schedule(world);
}

void Schedule::resolve_system_profiles() {
#if defined(FEI_ENABLE_TRACY) || defined(FEI_ENABLE_PROFILE_SUMMARY)
    for (auto& [_, config] : m_systems) {
        resolve_system_profile(config);
    }
#endif
}

void Schedule::build_execution_batches() {
    m_execution_batches.clear();

    const auto& sorted_nodes = m_graph.sorted_nodes();
    const auto& edges = m_graph.edges();

    std::unordered_map<SystemId, int> in_degree;
    std::unordered_set<SystemId> remaining;
    for (auto node : sorted_nodes) {
        in_degree[node] = 0;
        remaining.insert(node);
    }
    for (const auto& [_, tos] : edges) {
        for (auto to : tos) {
            in_degree[to]++;
        }
    }

    while (!remaining.empty()) {
        std::vector<SystemId> batch;
        for (auto system_id : sorted_nodes) {
            if (!remaining.contains(system_id) || in_degree[system_id] != 0) {
                continue;
            }

            auto access = effective_access(m_systems.at(system_id));
            bool has_conflict = false;
            for (auto batch_system_id : batch) {
                auto batch_access =
                    effective_access(m_systems.at(batch_system_id));
                if (access.conflicts_with(batch_access)) {
                    has_conflict = true;
                    break;
                }
            }
            if (has_conflict) {
                if (access.is_barrier()) {
                    break;
                }
                continue;
            }

            batch.push_back(system_id);
            if (access.is_barrier()) {
                break;
            }
        }

        if (batch.empty()) {
            fei::fatal("Unable to build system execution batch");
        }

        for (auto system_id : batch) {
            remaining.erase(system_id);
            auto it = edges.find(system_id);
            if (it == edges.end()) {
                continue;
            }
            for (auto to : it->second) {
                --in_degree[to];
            }
        }
        m_execution_batches.push_back(std::move(batch));
    }
}

} // namespace fei
