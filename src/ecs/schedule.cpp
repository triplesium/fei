#include "ecs/schedule.hpp"

#include "ecs/fwd.hpp"
#include "ecs/system.hpp"
#include "ecs/world.hpp"

#include <print>
#include <queue>
#include <unordered_map>

namespace fei {

SystemId SystemConfig::next_id = 0;

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
        it->second.run_systems(world);
    }
}

void Schedule::run_systems(World& world) {
    for (auto system_id : m_graph.sorted_nodes()) {
        auto it = m_systems.find(system_id);
        if (it != m_systems.end()) {
            it->second.system->run(world);
        }
    }
}

} // namespace fei
