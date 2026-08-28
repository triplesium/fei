#include "ecs/schedule.hpp"

#include "ecs/commands.hpp"
#include "ecs/execution_lane.hpp"
#include "ecs/fwd.hpp"
#include "ecs/system.hpp"
#include "ecs/world.hpp"

#include <algorithm>
#include <exception>
#include <future>
#include <queue>
#include <unordered_map>
#include <unordered_set>

#if defined(ETS_ENABLE_TRACY) || defined(ETS_ENABLE_PROFILE_SUMMARY)
#    include "ecs/system_profile.hpp"
#    include "profiling/profiling.hpp"

#    include <string>
#endif

namespace ets {
namespace {

#if defined(ETS_ENABLE_TRACY) || defined(ETS_ENABLE_PROFILE_SUMMARY)
void resolve_system_profile(SystemConfig& config) {
    if (config.system->has_profile_key()) {
        auto& registry = SystemProfileRegistry::instance();
        const auto profile_key = config.system->profile_key();
        if (!config.profile.symbol.valid()) {
            config.profile.symbol = registry.symbol_ref(profile_key);
        }

        if (!config.profile.named()) {
            auto profile = registry.symbolize(profile_key);
            if (!profile) {
                profile = registry.find(profile_key);
            }
            if (profile && profile->named()) {
                profile->symbol = config.profile.symbol;
                config.profile = std::move(*profile);
            }
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
#if defined(ETS_ENABLE_TRACY) || defined(ETS_ENABLE_PROFILE_SUMMARY)
    ETS_PROFILE_SYSTEM_SCOPE(schedule, config.id, config.profile);
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

void queue_batch_deferred(
    const std::vector<SystemId>& batch,
    std::unordered_map<SystemId, SystemConfig>& systems,
    CommandsQueue& target
) {
    for (const auto system_id : batch) {
        auto it = systems.find(system_id);
        if (it != systems.end()) {
            it->second.system->queue_deferred(target);
        }
    }
}

} // namespace

SystemId SystemConfig::next_id = 0;

Schedules::Schedules() : m_thread_pool(std::make_unique<ThreadPool>()) {}

SystemHandle Schedules::add_system(ScheduleId schedule, SystemConfig config) {
    auto handle = SystemHandle {
        .schedule = schedule,
        .id = m_schedules[schedule].add_system(std::move(config))
    };
    ++m_topology_generation;
    return handle;
}

bool Schedules::remove_system(SystemHandle handle) {
    auto it = m_schedules.find(handle.schedule);
    if (it == m_schedules.end()) {
        return false;
    }
    const auto removed = it->second.remove_system(handle.id);
    if (removed) {
        ++m_topology_generation;
    }
    return removed;
}

bool Schedules::replace_system(SystemHandle handle, SystemConfig config) {
    auto it = m_schedules.find(handle.schedule);
    if (it == m_schedules.end()) {
        return false;
    }
    const auto replaced =
        it->second.replace_system(handle.id, std::move(config));
    if (replaced) {
        ++m_topology_generation;
    }
    return replaced;
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
        ets::fatal("Cycle detected in system dependencies");
    }
}

void Schedules::run_systems(ScheduleId schedule, World& world) {
    auto it = m_schedules.find(schedule);
    if (it != m_schedules.end()) {
        it->second.run_systems(schedule, world, *m_thread_pool);
    }
}

Optional<ScheduleDebugInfo> Schedules::debug_info(ScheduleId schedule) {
    auto it = m_schedules.find(schedule);
    if (it == m_schedules.end()) {
        return nullopt;
    }
    return it->second.debug_info(schedule);
}

Result<ScheduleRuntimeState, RuntimeStateError>
Schedule::capture_runtime_state(ScheduleId schedule) const {
    ScheduleRuntimeState result {.id = schedule};
    std::vector<SystemId> ids;
    ids.reserve(m_systems.size());
    for (const auto& [id, _] : m_systems) {
        ids.push_back(id);
    }
    std::ranges::sort(ids);
    result.systems.reserve(ids.size());
    for (const auto id : ids) {
        const auto& config = m_systems.at(id);
        auto system = config.system->capture_runtime_state();
        if (!system) {
            auto error = std::move(system.error());
            error.path = "systems[" + std::to_string(id) + "]." + error.path;
            return failure(std::move(error));
        }
        ScheduledSystemRuntimeState state {
            .id = id,
            .system = std::move(*system),
        };
        state.conditions.reserve(config.conditions.size());
        for (std::size_t index = 0; index < config.conditions.size(); ++index) {
            auto condition = config.conditions[index]->capture_runtime_state();
            if (!condition) {
                auto error = std::move(condition.error());
                error.path = "systems[" + std::to_string(id) + "].conditions[" +
                             std::to_string(index) + "]." + error.path;
                return failure(std::move(error));
            }
            state.conditions.push_back(std::move(*condition));
        }
        result.systems.push_back(std::move(state));
    }
    return result;
}

Status<RuntimeStateError>
Schedule::validate_runtime_state(const ScheduleRuntimeState& state) const {
    if (state.systems.size() != m_systems.size()) {
        return failure(
            RuntimeStateError {
                .path = "systems",
                .message = "Schedule system count changed since checkpoint",
            }
        );
    }
    for (const auto& snapshot : state.systems) {
        const auto found = m_systems.find(snapshot.id);
        if (found == m_systems.end()) {
            return failure(
                RuntimeStateError {
                    .path = "systems[" + std::to_string(snapshot.id) + "]",
                    .message = "Scheduled system no longer exists",
                }
            );
        }
        const auto& config = found->second;
        if (snapshot.conditions.size() != config.conditions.size()) {
            return failure(
                RuntimeStateError {
                    .path = "systems[" + std::to_string(snapshot.id) +
                            "].conditions",
                    .message =
                        "System condition count changed since checkpoint",
                }
            );
        }
        auto valid = config.system->validate_runtime_state(snapshot.system);
        if (!valid) {
            auto error = std::move(valid.error());
            error.path =
                "systems[" + std::to_string(snapshot.id) + "]." + error.path;
            return failure(std::move(error));
        }
        for (std::size_t index = 0; index < snapshot.conditions.size();
             ++index) {
            valid = config.conditions[index]->validate_runtime_state(
                snapshot.conditions[index]
            );
            if (!valid) {
                auto error = std::move(valid.error());
                error.path = "systems[" + std::to_string(snapshot.id) +
                             "].conditions[" + std::to_string(index) + "]." +
                             error.path;
                return failure(std::move(error));
            }
        }
    }
    return {};
}

Status<RuntimeStateError>
Schedule::restore_runtime_state(const ScheduleRuntimeState& state) {
    auto valid = validate_runtime_state(state);
    if (!valid) {
        return valid;
    }
    for (const auto& snapshot : state.systems) {
        auto& config = m_systems.at(snapshot.id);
        auto restored = config.system->restore_runtime_state(snapshot.system);
        if (!restored) {
            return restored;
        }
        for (std::size_t index = 0; index < snapshot.conditions.size();
             ++index) {
            restored = config.conditions[index]->restore_runtime_state(
                snapshot.conditions[index]
            );
            if (!restored) {
                return restored;
            }
        }
    }
    return {};
}

Result<SchedulesRuntimeState, RuntimeStateError>
Schedules::capture_runtime_state() const {
    SchedulesRuntimeState result {
        .topology_generation = m_topology_generation,
    };
    std::vector<ScheduleId> ids;
    ids.reserve(m_schedules.size());
    for (const auto& [id, _] : m_schedules) {
        ids.push_back(id);
    }
    std::ranges::sort(ids);
    result.schedules.reserve(ids.size());
    for (const auto id : ids) {
        auto state = m_schedules.at(id).capture_runtime_state(id);
        if (!state) {
            auto error = std::move(state.error());
            error.path = "schedules[" + std::to_string(id) + "]." + error.path;
            return failure(std::move(error));
        }
        result.schedules.push_back(std::move(*state));
    }
    return result;
}

Status<RuntimeStateError>
Schedules::validate_runtime_state(const SchedulesRuntimeState& state) const {
    if (state.topology_generation != m_topology_generation) {
        return failure(
            RuntimeStateError {
                .path = "topology_generation",
                .message = "Schedule topology changed since checkpoint",
            }
        );
    }
    if (state.schedules.size() != m_schedules.size()) {
        return failure(
            RuntimeStateError {
                .path = "schedules",
                .message = "Schedule count changed since checkpoint",
            }
        );
    }
    for (const auto& snapshot : state.schedules) {
        const auto found = m_schedules.find(snapshot.id);
        if (found == m_schedules.end()) {
            return failure(
                RuntimeStateError {
                    .path = "schedules[" + std::to_string(snapshot.id) + "]",
                    .message = "Schedule no longer exists",
                }
            );
        }
        auto valid = found->second.validate_runtime_state(snapshot);
        if (!valid) {
            auto error = std::move(valid.error());
            error.path =
                "schedules[" + std::to_string(snapshot.id) + "]." + error.path;
            return failure(std::move(error));
        }
    }
    return {};
}

Status<RuntimeStateError>
Schedules::restore_runtime_state(const SchedulesRuntimeState& state) {
    auto valid = validate_runtime_state(state);
    if (!valid) {
        return valid;
    }
    for (const auto& snapshot : state.schedules) {
        auto restored =
            m_schedules.at(snapshot.id).restore_runtime_state(snapshot);
        if (!restored) {
            auto error = std::move(restored.error());
            error.path =
                "schedules[" + std::to_string(snapshot.id) + "]." + error.path;
            return failure(std::move(error));
        }
    }
    return {};
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
        ets::fatal("System with id {} has already been added", id);
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
    m_system_set_members.clear();
    m_graph.clear();
    m_execution_batches.clear();

    resolve_dependencies();
    resolve_system_profiles();
    build_graph();
    m_graph.sort();
    build_execution_batches();
    m_profile_records_ready = false;
    m_dirty = false;
}

ScheduleDebugInfo Schedule::debug_info(ScheduleId schedule) {
    ensure_execution_plan();
    ScheduleDebugInfo debug {
        .id = schedule,
        .batches = m_execution_batches,
    };
    std::unordered_map<SystemId, std::vector<SystemId>> incoming;
    for (const auto& [from, targets] : m_graph.edges()) {
        for (auto target : targets) {
            incoming[target].push_back(from);
        }
    }
    for (auto& [_, dependencies] : incoming) {
        std::ranges::sort(dependencies);
        dependencies.erase(
            std::unique(dependencies.begin(), dependencies.end()),
            dependencies.end()
        );
    }
    std::unordered_map<SystemId, std::size_t> batch_indices;
    for (std::size_t batch_index = 0; batch_index < m_execution_batches.size();
         ++batch_index) {
        for (auto system : m_execution_batches[batch_index]) {
            batch_indices[system] = batch_index;
        }
    }
    const auto& sorted = m_graph.sorted_nodes();
    debug.systems.reserve(sorted.size());
    for (std::size_t index = 0; index < sorted.size(); ++index) {
        const auto id = sorted[index];
        const auto& config = m_systems.at(id);
        debug.systems.push_back(
            SystemScheduleDebugInfo {
                .id = id,
                .name = config.profile.named() ? config.profile.name :
                                                 "system#" + std::to_string(id),
                .dependencies = std::move(incoming[id]),
                .topological_index = index,
                .batch_index = batch_indices[id],
            }
        );
    }
    return debug;
}

void Schedule::run_systems(World& world) {
    run_systems(0, world);
}

void Schedule::run_systems(ScheduleId schedule, World& world) {
    ensure_execution_plan();
    prepare_system_profile_records(schedule);

    auto run_one = [this, schedule, &world](SystemId system_id) {
        const detail::SystemExecutionLaneScope lane_scope {
            SystemExecutionLane {.index = 0, .count = 1, .caller = true}
        };
        auto it = m_systems.find(system_id);
        if (it != m_systems.end() && should_run(it->second, world)) {
            run_profiled_system(schedule, it->second, world);
        }
    };

    for (const auto& batch : m_execution_batches) {
        try {
            for (auto system_id : batch) {
                run_one(system_id);
            }
        } catch (...) {
            CommandsQueue discarded;
            queue_batch_deferred(batch, m_systems, discarded);
            throw;
        }
        auto& commands = world.resource<CommandsQueue>();
        queue_batch_deferred(batch, m_systems, commands);
        if (m_apply_deferred) {
            commands.execute_after_batch(world);
        }
    }
    if (m_apply_deferred) {
        world.resource<CommandsQueue>().execute_after_schedule(world);
    }
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
    prepare_system_profile_records(schedule);

    const auto lane_count = thread_pool.thread_count() + 1;
    auto run_one =
        [this, schedule, &world, &thread_pool, lane_count](SystemId system_id) {
            const auto worker = thread_pool.current_worker_index();
            const detail::SystemExecutionLaneScope lane_scope {
                worker ?
                    SystemExecutionLane {
                        .index = *worker,
                        .count = lane_count,
                        .caller = false,
                    } :
                    SystemExecutionLane {
                        .index = lane_count - 1,
                        .count = lane_count,
                        .caller = true,
                    }
            };
            auto it = m_systems.find(system_id);
            if (it != m_systems.end() && should_run(it->second, world)) {
                run_profiled_system(schedule, it->second, world);
            }
        };

    for (const auto& batch : m_execution_batches) {
        std::exception_ptr exception;
        if (batch.size() == 1 || thread_pool.thread_count() <= 1) {
            try {
                for (auto system_id : batch) {
                    run_one(system_id);
                }
            } catch (...) {
                exception = std::current_exception();
            }
        } else {
            std::vector<std::future<void>> jobs;
            jobs.reserve(batch.size());
            for (auto system_id : batch) {
                jobs.push_back(thread_pool.submit([run_one, system_id]() {
                    run_one(system_id);
                }));
            }

            for (auto& job : jobs) {
                try {
                    job.get();
                } catch (...) {
                    if (!exception) {
                        exception = std::current_exception();
                    }
                }
            }
        }
        if (exception) {
            CommandsQueue discarded;
            queue_batch_deferred(batch, m_systems, discarded);
            std::rethrow_exception(exception);
        }
        auto& commands = world.resource<CommandsQueue>();
        queue_batch_deferred(batch, m_systems, commands);
        if (m_apply_deferred) {
            commands.execute_after_batch(world);
        }
    }
    if (m_apply_deferred) {
        world.resource<CommandsQueue>().execute_after_schedule(world);
    }
}

void Schedule::resolve_system_profiles() {
#if defined(ETS_ENABLE_TRACY) || defined(ETS_ENABLE_PROFILE_SUMMARY)
    for (auto& [_, config] : m_systems) {
        resolve_system_profile(config);
    }
#endif
}

void Schedule::prepare_system_profile_records(ScheduleId schedule) {
#if defined(ETS_ENABLE_PROFILE_SUMMARY)
    if (m_profile_records_ready && m_profile_schedule_id == schedule) {
        return;
    }
    for (auto& [_, config] : m_systems) {
        config.profile.record_id = register_system_profile_record(
            schedule,
            config.id,
            &config.profile.symbol,
            config.profile.name,
            config.profile.file,
            config.profile.function,
            config.profile.line
        );
    }
#else
    (void)schedule;
#endif
    m_profile_schedule_id = schedule;
    m_profile_records_ready = true;
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
            ets::fatal("Unable to build system execution batch");
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

} // namespace ets
