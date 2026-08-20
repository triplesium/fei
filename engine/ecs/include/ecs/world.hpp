#pragma once

#include "base/optional.hpp"
#include "base/result.hpp"
#include "ecs/archetype.hpp"
#include "ecs/entity.hpp"
#include "ecs/fwd.hpp"
#include "ecs/hierarchy.hpp"
#include "ecs/removal_detection.hpp"
#include "ecs/resource.hpp"
#include "ecs/schedule.hpp"
#include "ecs/system.hpp"
#include "refl/ref_utils.hpp"

#include <atomic>
#include <concepts>
#include <cstddef>
#include <memory>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace fei {

class World;

template<typename T>
class State;

template<typename T>
class NextState;

template<typename T>
concept FromWorld = std::constructible_from<T, World&>;

enum class RegisteredSystemError {
    NotFound,
    AlreadyRunning,
};

class World {
  private:
    struct RegisteredSystem {
        std::unique_ptr<System> system;
        bool running {false};
    };

    Entities m_entities;
    Resources m_resources;
    // Components may own resource-backed handles. Declare archetypes after
    // resources so components are destroyed before the resources they use.
    Archetypes m_archetypes;
    Schedules m_schedules;
    std::unordered_map<SystemId, RegisteredSystem> m_registered_systems;
    SystemId m_next_registered_system_id {0};
    std::uint64_t m_registered_system_generation {0};
    std::size_t m_registered_system_execution_depth {0};
    std::atomic<Tick> m_change_tick {0};
    RemovedComponentEvents m_removed_components;

  public:
    World() = default;
    World(const World&) = delete;
    World& operator=(const World&) = delete;

    World(World&& other) noexcept :
        m_entities(std::move(other.m_entities)),
        m_resources(std::move(other.m_resources)),
        m_archetypes(std::move(other.m_archetypes)),
        m_schedules(std::move(other.m_schedules)),
        m_registered_systems(std::move(other.m_registered_systems)),
        m_next_registered_system_id(other.m_next_registered_system_id),
        m_registered_system_generation(other.m_registered_system_generation),
        m_change_tick(other.read_change_tick()),
        m_removed_components(std::move(other.m_removed_components)) {}

    World& operator=(World&& other) noexcept {
        if (this != &other) {
            m_entities = std::move(other.m_entities);
            // Release components while their backing resources still exist.
            m_archetypes = std::move(other.m_archetypes);
            m_resources = std::move(other.m_resources);
            m_schedules = std::move(other.m_schedules);
            m_registered_systems = std::move(other.m_registered_systems);
            m_next_registered_system_id = other.m_next_registered_system_id;
            m_registered_system_generation =
                other.m_registered_system_generation;
            m_registered_system_execution_depth = 0;
            m_change_tick.store(
                other.read_change_tick(),
                std::memory_order_relaxed
            );
            m_removed_components = std::move(other.m_removed_components);
        }
        return *this;
    }

    Entity entity();
    Entity reserve_entity() { return m_entities.reserve(); }
    void materialize_entity(Entity entity);
    void add_component(Entity entity, Ref ref);
    template<typename T>
    void add_component(Entity entity, T val) {
        using U = std::remove_cvref_t<T>;
        if constexpr (std::same_as<U, ChildOf>) {
            set_parent(entity, val.parent);
        } else {
            add_component(entity, make_ref<T>(val));
        }
    }
    void remove_component(Entity entity, TypeId type_id);
    template<typename T>
    void remove_component(Entity entity) {
        if constexpr (std::same_as<std::remove_cvref_t<T>, ChildOf>) {
            remove_parent(entity);
        } else {
            remove_component(entity, type_id<T>());
        }
    }
    bool has_component(Entity entity, TypeId type_id) const;
    template<typename T>
    bool has_component(Entity entity) const {
        return has_component(entity, type_id<T>());
    }
    Ref get_component(Entity entity, TypeId type_id);
    Ref get_component(Entity entity, TypeId type_id) const;
    bool mark_component_changed(Entity entity, TypeId type_id);
    template<typename T>
    const T& get_component(Entity entity) {
        return static_cast<const World&>(*this).get_component<T>(entity);
    }
    template<typename T>
    const T& get_component(Entity entity) const {
        return get_component(entity, type_id<T>()).template get_const<T>();
    }

    template<typename T>
    ComponentRW<std::remove_cvref_t<T>> get_component_rw(Entity entity) {
        using U = std::remove_cvref_t<T>;
        auto location = m_entities.get_location(entity);
        auto& archetype = m_archetypes.get(location.archetype_id);
        auto ref = archetype.get_component(type_id<U>(), location.row);
        return ComponentRW<U>(
            ref.template get<U>(),
            archetype.component_ticks(type_id<U>(), location.row),
            m_change_tick
        );
    }

    template<typename T>
    bool mark_component_changed(Entity entity) {
        return mark_component_changed(entity, type_id<T>());
    }

    bool has_entity(Entity entity) const { return m_entities.contains(entity); }

    Optional<EntityLocation> entity_location(Entity entity) const {
        if (!has_entity(entity)) {
            return nullopt;
        }
        return m_entities.get_location(entity);
    }

    Tick read_change_tick() const {
        return m_change_tick.load(std::memory_order_relaxed);
    }

    Tick increment_change_tick() {
        return m_change_tick.fetch_add(1, std::memory_order_relaxed) + 1;
    }

    Result<WorldRuntimeState, RuntimeStateError> capture_runtime_state() const;
    Status<RuntimeStateError>
    validate_runtime_state(const WorldRuntimeState& state) const;
    Status<RuntimeStateError>
    restore_runtime_state(const WorldRuntimeState& state);

    void set_parent(Entity child, Entity parent);
    void remove_parent(Entity child);
    bool has_parent(Entity child) const;
    Optional<Entity> parent(Entity child) const;
    void despawn(Entity entity);

    void clear_trackers() { m_removed_components.update(); }

    [[nodiscard]] const RemovedComponentBuffer*
    removed_components(TypeId component) const {
        return m_removed_components.get(component);
    }

    SystemHandle add_system(ScheduleId schedule, SystemConfig config) {
        return m_schedules.add_system(schedule, std::move(config));
    }

    std::vector<SystemHandle> add_systems(
        ScheduleId schedule,
        std::convertible_to<SystemConfigs> auto&&... configs
    ) {
        return m_schedules.add_systems(
            schedule,
            std::forward<decltype(configs)>(configs)...
        );
    }

    bool remove_system(SystemHandle handle) {
        return m_schedules.remove_system(handle);
    }

    bool replace_system(SystemHandle handle, SystemConfig config) {
        return m_schedules.replace_system(handle, std::move(config));
    }

    void run_schedule(ScheduleId schedule) {
        m_schedules.run_systems(schedule, *this);
    }

    void set_schedule_apply_deferred(ScheduleId schedule, bool enabled) {
        m_schedules.set_apply_deferred(schedule, enabled);
    }

    void apply_deferred();

    Optional<ScheduleDebugInfo> schedule_debug_info(ScheduleId schedule) {
        return m_schedules.debug_info(schedule);
    }

    void sort_systems() { m_schedules.sort_systems(); }

    void set_worker_threads(std::size_t thread_count) {
        m_schedules.set_worker_threads(thread_count);
    }

    std::size_t worker_threads() const { return m_schedules.worker_threads(); }

    // Snapshot restore uses these swaps to stage a new entity graph while the
    // previous graph remains available for lossless rollback. They exchange
    // storage only; schedules and unrelated resources stay with each World.
    void swap_entity_state(World& other) noexcept {
        using std::swap;
        swap(m_entities, other.m_entities);
        swap(m_archetypes, other.m_archetypes);
        swap(m_removed_components, other.m_removed_components);
    }

    void swap_resource(TypeId type, World& other) {
        m_resources.swap_entry(type, other.m_resources);
    }

    const std::vector<TypeId>& resource_types() const {
        return m_resources.types();
    }

    RegisteredSystemId register_system(std::unique_ptr<System> system);

    template<typename F>
        requires IntoSystem<std::decay_t<F>>
    RegisteredSystemId register_system(F&& func) {
        using Func = std::decay_t<F>;
        return register_system(
            std::make_unique<FunctionSystem<Func>>(std::forward<F>(func))
        );
    }

    template<typename F>
        requires IntoSystem<std::decay_t<F>> &&
                 std::copy_constructible<std::decay_t<F>>
    RegisteredSystemId register_checkpointed_system(F&& func) {
        using Func = std::decay_t<F>;
        return register_system(
            std::make_unique<FunctionSystem<Func, true>>(std::forward<F>(func))
        );
    }

    Status<RegisteredSystemError> run_system(RegisteredSystemId id);

    Status<RegisteredSystemError> unregister_system(RegisteredSystemId id);

    void configure_sets(
        ScheduleId schedule,
        std::convertible_to<SystemSetConfigs> auto&&... configs
    ) {
        m_schedules.configure_sets(
            schedule,
            std::forward<decltype(configs)>(configs)...
        );
    }

    template<typename T>
    std::remove_cvref_t<T>& add_resource(T&& val) {
        using U = std::remove_cvref_t<T>;
        m_resources
            .set(type_id<U>(), increment_change_tick(), std::forward<T>(val));
        return m_resources.get_mut(type_id<U>()).template get<U>();
    }

    Ref add_resource(TypeId type_id, Val val) {
        m_resources.set(type_id, increment_change_tick(), std::move(val));
        return m_resources.get_mut(type_id);
    }

    template<typename T>
    std::remove_cvref_t<T>& add_keyed_resource(TypeId resource_id, T&& val) {
        using U = std::remove_cvref_t<T>;
        m_resources.template emplace<U, U>(
            resource_id,
            increment_change_tick(),
            std::forward<T>(val)
        );
        return m_resources.get_mut(resource_id).template get<U>();
    }

    template<typename T, typename U>
    T& add_resource_as(U&& val) {
        using Stored = std::remove_cvref_t<U>;
        if constexpr (std::derived_from<Stored, T>) {
            m_resources.template emplace<T, Stored>(
                type_id<T>(),
                increment_change_tick(),
                std::forward<U>(val)
            );
        } else {
            m_resources.template emplace<T, T>(
                type_id<T>(),
                increment_change_tick(),
                std::forward<U>(val)
            );
        }
        return m_resources.get_mut(type_id<T>()).template get<T>();
    }

    template<typename T>
    void add_readonly_resource_ref(T& resource) {
        using U = std::remove_cvref_t<T>;
        m_resources.set_readonly_ref(
            type_id<U>(),
            increment_change_tick(),
            static_cast<const U&>(resource)
        );
    }

    template<FromWorld T>
    T& init_resource() {
        T resource(*this);
        return add_resource(std::move(resource));
    }

    template<typename T>
    State<std::remove_cvref_t<T>>& init_state(T&& state);

    template<typename T>
    State<std::remove_cvref_t<T>>& insert_state(T&& state);

    void run_state_transitions();

    template<typename T>
    bool has_resource() const {
        return has_resource(type_id<T>());
    }

    bool has_resource(TypeId type_id) const {
        return m_resources.contains(type_id);
    }

    template<typename T>
    bool has_local_resource() const {
        return has_local_resource(type_id<T>());
    }

    bool has_local_resource(TypeId type_id) const {
        return m_resources.contains(type_id);
    }

    template<typename T>
    T& resource() {
        auto type = type_id<T>();
        auto ret = m_resources.get_mut(type);
        if (!ret) {
            fatal("Resource of type {} not found", type_name<T>());
        }
        if (ret.is_const()) {
            fatal("Resource of type {} is read-only", type_name<T>());
        }
        m_resources.ticks(type).mark_changed(increment_change_tick());
        return ret.template get<T>();
    }

    template<typename T>
    const T& resource() const {
        auto ret = m_resources.get(type_id<T>());
        if (!ret) {
            fatal("Resource of type {} not found", type_name<T>());
        }
        return ret.template get_const<T>();
    }

    Ref resource(TypeId type_id) {
        auto ret = m_resources.get_mut(type_id);
        if (!ret) {
            fatal("Resource with type id {} not found", type_id.id());
        }
        if (ret.is_const()) {
            fatal("Resource with type id {} is read-only", type_id.id());
        }
        m_resources.ticks(type_id).mark_changed(increment_change_tick());
        return ret;
    }

    Ref resource(TypeId type_id) const {
        auto ret = m_resources.get(type_id);
        if (!ret) {
            fatal("Resource with type id {} not found", type_id.id());
        }
        return ret;
    }

    Ref resource_untracked(TypeId type_id) {
        auto ret = m_resources.get_mut(type_id);
        if (!ret) {
            fatal("Resource with type id {} not found", type_id.id());
        }
        if (ret.is_const()) {
            fatal("Resource with type id {} is read-only", type_id.id());
        }
        return ret;
    }

    ComponentTicks& resource_ticks(TypeId type_id) {
        return m_resources.ticks(type_id);
    }

    const ComponentTicks& resource_ticks(TypeId type_id) const {
        return m_resources.ticks(type_id);
    }

    template<typename F>
    void run_system_once(F&& func) {
        FunctionSystem system(std::forward<F>(func));
        system.run(*this);
        queue_system_deferred(system);
        flush_system_commands();
    }

    Archetypes& archetypes() { return m_archetypes; }
    const Archetypes& archetypes() const { return m_archetypes; }

  private:
    Status<RegisteredSystemError> run_registered_system(RegisteredSystemId id);
    void queue_system_deferred(System& system);
    void flush_system_commands();

    void raw_add_component(Entity entity, Ref ref);
    void raw_remove_component(Entity entity, TypeId type_id);
    void raw_despawn(Entity entity);
    void add_child(Entity parent, Entity child);
    void remove_child(Entity parent, Entity child);
    bool would_create_cycle(Entity child, Entity parent) const;
};

} // namespace fei
