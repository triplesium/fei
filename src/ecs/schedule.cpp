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

namespace fei {

SystemId SystemConfig::next_id = 0;

Schedules::Schedules() : m_thread_pool(std::make_unique<ThreadPool>()) {}

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
        it->second.run_systems(world, *m_thread_pool);
    }
}

void Schedules::set_worker_threads(std::size_t thread_count) {
    m_thread_pool = std::make_unique<ThreadPool>(thread_count);
}

std::size_t Schedules::worker_threads() const {
    return m_thread_pool->thread_count();
}

void Schedule::run_systems(World& world) {
    for (const auto& batch : m_execution_batches) {
        for (auto system_id : batch) {
            auto it = m_systems.find(system_id);
            if (it != m_systems.end()) {
                it->second.system->run(world);
            }
        }
        world.resource<CommandsQueue>().execute(world);
    }
}

void Schedule::run_systems(World& world, ThreadPool& thread_pool) {
    auto run_one = [this, &world](SystemId system_id) {
        auto it = m_systems.find(system_id);
        if (it != m_systems.end()) {
            it->second.system->run(world);
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
        world.resource<CommandsQueue>().execute(world);
    }
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

            const auto& access = m_systems.at(system_id).system->access();
            bool has_conflict = false;
            for (auto batch_system_id : batch) {
                const auto& batch_access =
                    m_systems.at(batch_system_id).system->access();
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
