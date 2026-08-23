#include "scripting/state.hpp"

#include "base/optional.hpp"
#include "ecs/commands.hpp"
#include "ecs/dynamic/state.hpp"
#include "ecs/state.hpp"
#include "ecs/system_profile.hpp"
#include "ecs/world.hpp"
#include "refl/dynamic_type.hpp"
#include "refl/registry.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace ets {
namespace {

constexpr std::string_view c_variant_field = "__ets_state_variant";

struct ScriptStateStorage {
    Val current;
};

struct ScriptNextStateStorage {
    Optional<Val> pending;
};

struct ScriptStateTransitionStorage {
    bool initial_enter {true};
    Optional<Val> exited;
    Optional<Val> entered;
};

struct ScriptTransitionKey {
    std::uint64_t exited {0};
    std::uint64_t entered {0};

    bool operator==(const ScriptTransitionKey&) const = default;
};

struct ScriptTransitionKeyHash {
    std::size_t operator()(const ScriptTransitionKey& key) const {
        return static_cast<std::size_t>(
            key.exited ^ (key.entered + 0x9e3779b97f4a7c15ULL +
                          (key.exited << 6U) + (key.exited >> 2U))
        );
    }
};

struct ScriptStateRuntime {
    std::string name;
    TypeId value_type;
    TypeId state_resource;
    TypeId next_state_resource;
    TypeId transition_resource;

    mutable std::shared_mutex schema_mutex;
    std::unordered_set<std::uint64_t> allowed_values;

    std::mutex schedule_mutex;
    std::unordered_map<std::uint64_t, ScheduleId> enter_schedules;
    std::unordered_map<std::uint64_t, ScheduleId> exit_schedules;
    std::unordered_map<ScriptTransitionKey, ScheduleId, ScriptTransitionKeyHash>
        transition_schedules;
};

std::mutex c_script_states_mutex;
std::unordered_map<TypeId, std::shared_ptr<ScriptStateRuntime>> c_script_states;

ScriptError state_error(const ScriptStateDecl& state, std::string message) {
    return ScriptError {
        "Script state '" + state.qualified_name + "': " + std::move(message)
    };
}

const DynamicStructLayout* state_value_layout(TypeId type) {
    const auto* layout =
        Registry::instance().try_get_dynamic_struct_layout(type);
    if (layout == nullptr || layout->fields.size() != 1 ||
        layout->fields.front().name != c_variant_field ||
        layout->fields.front().type != type_id<std::uint64_t>()) {
        return nullptr;
    }
    return layout;
}

Optional<std::uint64_t> state_variant(Ref value, TypeId expected_type) {
    if (!value || value.type_id() != expected_type) {
        return nullopt;
    }
    const auto* layout = state_value_layout(expected_type);
    if (layout == nullptr) {
        return nullopt;
    }
    const auto* bytes = static_cast<const std::byte*>(value.const_ptr());
    return *reinterpret_cast<const std::uint64_t*>(
        bytes + layout->fields.front().offset
    );
}

Result<Val, ScriptError>
make_state_value(TypeId type_id, std::uint64_t variant, std::string context) {
    auto type = Registry::instance().try_get_type(type_id);
    if (!type) {
        return failure(ScriptError {std::move(type.error().message)});
    }
    const auto* layout = state_value_layout(type_id);
    if (layout == nullptr) {
        return failure(
            ScriptError {std::move(context) + " has an invalid value type"}
        );
    }
    Val result = Val::default_construct(*type);
    auto* bytes = static_cast<std::byte*>(result.ref().ptr());
    *reinterpret_cast<std::uint64_t*>(bytes + layout->fields.front().offset) =
        variant;
    return result;
}

bool values_equal(const Val& lhs, const Val& rhs) {
    if (!lhs || !rhs || lhs.type_id() != rhs.type_id()) {
        return false;
    }
    auto equal =
        lhs.type()->equals(lhs.ref().const_ptr(), rhs.ref().const_ptr());
    return equal && *equal;
}

template<typename Storage>
Storage& storage(World& world, TypeId resource) {
    return world.resource(resource).template get<Storage>();
}

template<typename Storage>
const Storage& storage(const World& world, TypeId resource) {
    return world.resource(resource).template get_const<Storage>();
}

ScheduleId enter_schedule(
    const std::shared_ptr<ScriptStateRuntime>& runtime,
    std::uint64_t variant
) {
    std::scoped_lock lock(runtime->schedule_mutex);
    auto [found, inserted] = runtime->enter_schedules.try_emplace(variant);
    if (inserted) {
        found->second = detail::allocate_state_schedule_id();
    }
    return found->second;
}

ScheduleId exit_schedule(
    const std::shared_ptr<ScriptStateRuntime>& runtime,
    std::uint64_t variant
) {
    std::scoped_lock lock(runtime->schedule_mutex);
    auto [found, inserted] = runtime->exit_schedules.try_emplace(variant);
    if (inserted) {
        found->second = detail::allocate_state_schedule_id();
    }
    return found->second;
}

ScheduleId transition_schedule(
    const std::shared_ptr<ScriptStateRuntime>& runtime,
    std::uint64_t exited,
    std::uint64_t entered
) {
    std::scoped_lock lock(runtime->schedule_mutex);
    auto [found, inserted] = runtime->transition_schedules.try_emplace(
        ScriptTransitionKey {.exited = exited, .entered = entered}
    );
    if (inserted) {
        found->second = detail::allocate_state_schedule_id();
    }
    return found->second;
}

std::shared_ptr<ScriptStateRuntime> find_script_state_runtime(TypeId type) {
    std::scoped_lock lock(c_script_states_mutex);
    const auto found = c_script_states.find(type);
    return found != c_script_states.end() ? found->second : nullptr;
}

std::shared_ptr<ScriptStateRuntime>
ensure_script_state_runtime(const ScriptStateDecl& state) {
    std::scoped_lock lock(c_script_states_mutex);
    if (const auto found = c_script_states.find(state.type_id);
        found != c_script_states.end()) {
        return found->second;
    }

    auto runtime = std::make_shared<ScriptStateRuntime>();
    runtime->name = state.qualified_name;
    runtime->value_type = state.type_id;
    runtime->state_resource = TypeId {state.qualified_name + ".__ets_State"};
    runtime->next_state_resource =
        TypeId {state.qualified_name + ".__ets_NextState"};
    runtime->transition_resource =
        TypeId {state.qualified_name + ".__ets_StateTransition"};
    c_script_states.emplace(state.type_id, runtime);

    DynamicStateRegistry::instance().add(
        DynamicStateOps {
            .value_type = runtime->value_type,
            .state_resource = runtime->state_resource,
            .next_state_resource = runtime->next_state_resource,
            .initialized =
                [runtime](const World& world) {
                    return world.has_resource(runtime->state_resource) &&
                           world.has_resource(runtime->next_state_resource);
                },
            .current = [runtime](World& world) -> Ref {
                if (!world.has_resource(runtime->state_resource)) {
                    return {};
                }
                return storage<ScriptStateStorage>(
                           static_cast<const World&>(world),
                           runtime->state_resource
                )
                    .current.ref();
            },
            .set_next = [runtime](World& world, Ref value)
                -> Status<DynamicSystemError> {
                auto variant = state_variant(value, runtime->value_type);
                if (!variant) {
                    return failure(
                        DynamicSystemError {
                            "NextState value does not belong to script "
                            "state '" +
                            runtime->name + "'"
                        }
                    );
                }
                {
                    std::shared_lock lock(runtime->schema_mutex);
                    if (!runtime->allowed_values.contains(*variant)) {
                        return failure(
                            DynamicSystemError {
                                "State value is not declared by script "
                                "state '" +
                                runtime->name + "'"
                            }
                        );
                    }
                }
                auto copied = Val::copy(value);
                if (!copied) {
                    return failure(
                        DynamicSystemError {std::move(copied.error().message)}
                    );
                }
                storage<ScriptNextStateStorage>(
                    world,
                    runtime->next_state_resource
                )
                    .pending = std::move(*copied);
                return {};
            },
            .clear_next =
                [runtime](World& world) {
                    if (world.has_resource(runtime->next_state_resource)) {
                        storage<ScriptNextStateStorage>(
                            world,
                            runtime->next_state_resource
                        )
                            .pending.reset();
                    }
                },
            .on_enter =
                [runtime](Ref value) {
                    return enter_schedule(
                        runtime,
                        *state_variant(value, runtime->value_type)
                    );
                },
            .on_exit =
                [runtime](Ref value) {
                    return exit_schedule(
                        runtime,
                        *state_variant(value, runtime->value_type)
                    );
                },
            .on_transition =
                [runtime](Ref exited, Ref entered) {
                    return transition_schedule(
                        runtime,
                        *state_variant(exited, runtime->value_type),
                        *state_variant(entered, runtime->value_type)
                    );
                },
        }
    );
    return runtime;
}

void prepare_transition(
    const std::shared_ptr<ScriptStateRuntime>& runtime,
    World& world
) {
    auto& current =
        storage<ScriptStateStorage>(world, runtime->state_resource).current;
    auto& next =
        storage<ScriptNextStateStorage>(world, runtime->next_state_resource);
    auto& transition = storage<ScriptStateTransitionStorage>(
        world,
        runtime->transition_resource
    );
    transition.exited.reset();
    transition.entered.reset();
    if (transition.initial_enter) {
        transition.initial_enter = false;
        transition.entered = current;
        return;
    }
    if (!next.pending) {
        return;
    }
    Val pending = std::move(*next.pending);
    next.pending.reset();
    if (values_equal(current, pending)) {
        return;
    }
    transition.exited = current;
    current = std::move(pending);
    transition.entered = current;
}

void run_exit(
    const std::shared_ptr<ScriptStateRuntime>& runtime,
    World& world
) {
    const auto& transition = storage<ScriptStateTransitionStorage>(
        static_cast<const World&>(world),
        runtime->transition_resource
    );
    if (transition.exited) {
        world.run_schedule(exit_schedule(
            runtime,
            *state_variant(transition.exited->ref(), runtime->value_type)
        ));
    }
}

void run_transition(
    const std::shared_ptr<ScriptStateRuntime>& runtime,
    World& world
) {
    const auto& transition = storage<ScriptStateTransitionStorage>(
        static_cast<const World&>(world),
        runtime->transition_resource
    );
    if (transition.exited && transition.entered) {
        world.run_schedule(transition_schedule(
            runtime,
            *state_variant(transition.exited->ref(), runtime->value_type),
            *state_variant(transition.entered->ref(), runtime->value_type)
        ));
    }
}

void run_enter(
    const std::shared_ptr<ScriptStateRuntime>& runtime,
    World& world
) {
    const auto& transition = storage<ScriptStateTransitionStorage>(
        static_cast<const World&>(world),
        runtime->transition_resource
    );
    if (transition.entered) {
        world.run_schedule(enter_schedule(
            runtime,
            *state_variant(transition.entered->ref(), runtime->value_type)
        ));
    }
}

Status<ScriptError> validate_existing_state(
    World& world,
    const ScriptStateDecl& state,
    const std::shared_ptr<ScriptStateRuntime>& runtime,
    const std::unordered_set<std::uint64_t>& allowed
) {
    if (!world.has_resource(runtime->state_resource)) {
        return {};
    }
    const auto& current = storage<ScriptStateStorage>(
        static_cast<const World&>(world),
        runtime->state_resource
    );
    auto current_variant = state_variant(current.current.ref(), state.type_id);
    if (!current_variant || !allowed.contains(*current_variant)) {
        return failure(
            state_error(state, "reload removes the currently active value")
        );
    }
    if (!world.has_resource(runtime->next_state_resource)) {
        return {};
    }
    const auto& next = storage<ScriptNextStateStorage>(
        static_cast<const World&>(world),
        runtime->next_state_resource
    );
    if (next.pending) {
        auto pending = state_variant(next.pending->ref(), state.type_id);
        if (!pending || !allowed.contains(*pending)) {
            return failure(
                state_error(state, "reload removes the pending next value")
            );
        }
    }
    return {};
}

Status<ScriptError>
install_script_state(World& world, const ScriptStateDecl& state) {
    auto ensured = ensure_script_state_type(state);
    if (!ensured) {
        return ensured;
    }
    auto runtime = ensure_script_state_runtime(state);
    std::unordered_set<std::uint64_t> allowed;
    for (const auto& value : state.values) {
        allowed.insert(value.id);
    }
    auto valid = validate_existing_state(world, state, runtime, allowed);
    if (!valid) {
        return valid;
    }

    if (!world.has_resource(runtime->state_resource)) {
        auto initial = make_script_state_value(state, state.initial);
        if (!initial) {
            return failure(std::move(initial.error()));
        }
        world.add_keyed_resource(
            runtime->state_resource,
            ScriptStateStorage {.current = std::move(*initial)}
        );
    }
    if (!world.has_resource(runtime->next_state_resource)) {
        world.add_keyed_resource(
            runtime->next_state_resource,
            ScriptNextStateStorage {}
        );
    }
    const bool install_transition_runtime =
        !world.has_resource(runtime->transition_resource);
    if (install_transition_runtime) {
        world.add_keyed_resource(
            runtime->transition_resource,
            ScriptStateTransitionStorage {}
        );
    }
    {
        std::unique_lock lock(runtime->schema_mutex);
        runtime->allowed_values = std::move(allowed);
    }

    if (!install_transition_runtime) {
        return {};
    }
    if (!world.has_resource<CommandsQueue>()) {
        world.add_resource(CommandsQueue {});
    }
    world.configure_sets(
        StateTransition,
        chain(
            detail::StateTransitionSystems::Apply {},
            detail::StateTransitionSystems::Exit {},
            detail::StateTransitionSystems::Transition {},
            detail::StateTransitionSystems::Enter {}
        )
    );
    const auto system_name = [&](std::string_view phase) {
        return state.qualified_name + ".state_transition." +
               std::string {phase};
    };
    world.add_systems(
        StateTransition,
        named_system(
            system_name("apply"),
            [runtime](WorldRef world) {
                prepare_transition(runtime, *world);
            }
        ) | in_set<detail::StateTransitionSystems::Apply>(),
        named_system(
            system_name("exit"),
            [runtime](WorldRef world) {
                run_exit(runtime, *world);
            }
        ) | in_set<detail::StateTransitionSystems::Exit>(),
        named_system(
            system_name("transition"),
            [runtime](WorldRef world) {
                run_transition(runtime, *world);
            }
        ) | in_set<detail::StateTransitionSystems::Transition>(),
        named_system(system_name("enter"), [runtime](WorldRef world) {
            run_enter(runtime, *world);
        }) | in_set<detail::StateTransitionSystems::Enter>()
    );
    return {};
}

} // namespace

Status<ScriptError> ensure_script_state_type(const ScriptStateDecl& state) {
    auto& registry = Registry::instance();
    registry.register_type<std::uint64_t>();
    if (auto existing = registry.try_get_type(state.type_id)) {
        if (state_value_layout(state.type_id) == nullptr) {
            return failure(state_error(
                state,
                "type id conflicts with existing type '" + existing->name() +
                    "'"
            ));
        }
        ensure_script_state_runtime(state);
        return {};
    }
    auto registered = registry.register_dynamic_struct(
        DynamicStructDesc {
            .name = state.qualified_name,
            .id = state.type_id,
            .fields = {
                DynamicFieldDesc {
                    .name = std::string {c_variant_field},
                    .type = type_id<std::uint64_t>(),
                },
            },
        }
    );
    if (!registered) {
        return failure(state_error(state, registered.error().message));
    }
    ensure_script_state_runtime(state);
    return {};
}

Result<Val, ScriptError> make_script_state_value(
    const ScriptStateDecl& state,
    std::string_view value_name
) {
    const auto found = std::ranges::find(
        state.values,
        value_name,
        &ScriptStateValueDecl::name
    );
    if (found == state.values.end()) {
        return failure(state_error(
            state,
            "has no value '" + std::string {value_name} + "'"
        ));
    }
    return make_state_value(state.type_id, found->id, state.qualified_name);
}

Status<ScriptError>
install_script_module_states(World& world, const ScriptModuleDecl& module) {
    for (const auto& state : module.states) {
        auto installed = install_script_state(world, state);
        if (!installed) {
            return installed;
        }
    }
    return {};
}

bool is_script_state_type(TypeId type) {
    return find_script_state_runtime(type) != nullptr;
}

} // namespace ets
