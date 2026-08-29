#include "scripting/detail/state.hpp"

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

struct LuauStateStorage {
    Val current;
};

struct LuauNextStateStorage {
    Optional<Val> pending;
};

struct LuauStateTransitionStorage {
    bool initial_enter {true};
    Optional<Val> exited;
    Optional<Val> entered;
};

struct LuauTransitionKey {
    std::uint64_t exited {0};
    std::uint64_t entered {0};

    bool operator==(const LuauTransitionKey&) const = default;
};

struct LuauTransitionKeyHash {
    std::size_t operator()(const LuauTransitionKey& key) const {
        return static_cast<std::size_t>(
            key.exited ^ (key.entered + 0x9e3779b97f4a7c15ULL +
                          (key.exited << 6U) + (key.exited >> 2U))
        );
    }
};

struct LuauStateRuntime {
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
    std::unordered_map<LuauTransitionKey, ScheduleId, LuauTransitionKeyHash>
        transition_schedules;
};

std::mutex c_luau_states_mutex;
std::unordered_map<TypeId, std::shared_ptr<LuauStateRuntime>> c_luau_states;

LuauScriptError
luau_state_error(const LuauStateDecl& state, std::string message) {
    return LuauScriptError {
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

Result<Val, LuauScriptError>
make_state_value(TypeId type_id, std::uint64_t variant, std::string context) {
    auto type = Registry::instance().try_get_type(type_id);
    if (!type) {
        return failure(LuauScriptError {std::move(type.error().message)});
    }
    const auto* layout = state_value_layout(type_id);
    if (layout == nullptr) {
        return failure(
            LuauScriptError {std::move(context) + " has an invalid value type"}
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
    const std::shared_ptr<LuauStateRuntime>& runtime,
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
    const std::shared_ptr<LuauStateRuntime>& runtime,
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
    const std::shared_ptr<LuauStateRuntime>& runtime,
    std::uint64_t exited,
    std::uint64_t entered
) {
    std::scoped_lock lock(runtime->schedule_mutex);
    auto [found, inserted] = runtime->transition_schedules.try_emplace(
        LuauTransitionKey {.exited = exited, .entered = entered}
    );
    if (inserted) {
        found->second = detail::allocate_state_schedule_id();
    }
    return found->second;
}

std::shared_ptr<LuauStateRuntime> find_luau_state_runtime(TypeId type) {
    std::scoped_lock lock(c_luau_states_mutex);
    const auto found = c_luau_states.find(type);
    return found != c_luau_states.end() ? found->second : nullptr;
}

std::shared_ptr<LuauStateRuntime>
ensure_luau_state_runtime(const LuauStateDecl& state) {
    std::scoped_lock lock(c_luau_states_mutex);
    if (const auto found = c_luau_states.find(state.type_id);
        found != c_luau_states.end()) {
        return found->second;
    }

    auto runtime = std::make_shared<LuauStateRuntime>();
    runtime->name = state.qualified_name;
    runtime->value_type = state.type_id;
    runtime->state_resource = TypeId {state.qualified_name + ".__ets_State"};
    runtime->next_state_resource =
        TypeId {state.qualified_name + ".__ets_NextState"};
    runtime->transition_resource =
        TypeId {state.qualified_name + ".__ets_StateTransition"};
    c_luau_states.emplace(state.type_id, runtime);

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
                return storage<LuauStateStorage>(
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
                storage<LuauNextStateStorage>(
                    world,
                    runtime->next_state_resource
                )
                    .pending = std::move(*copied);
                return {};
            },
            .clear_next =
                [runtime](World& world) {
                    if (world.has_resource(runtime->next_state_resource)) {
                        storage<LuauNextStateStorage>(
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
    const std::shared_ptr<LuauStateRuntime>& runtime,
    World& world
) {
    auto& current =
        storage<LuauStateStorage>(world, runtime->state_resource).current;
    auto& next =
        storage<LuauNextStateStorage>(world, runtime->next_state_resource);
    auto& transition = storage<LuauStateTransitionStorage>(
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

void run_exit(const std::shared_ptr<LuauStateRuntime>& runtime, World& world) {
    const auto& transition = storage<LuauStateTransitionStorage>(
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
    const std::shared_ptr<LuauStateRuntime>& runtime,
    World& world
) {
    const auto& transition = storage<LuauStateTransitionStorage>(
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

void run_enter(const std::shared_ptr<LuauStateRuntime>& runtime, World& world) {
    const auto& transition = storage<LuauStateTransitionStorage>(
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

Status<LuauScriptError> validate_existing_state(
    World& world,
    const LuauStateDecl& state,
    const std::shared_ptr<LuauStateRuntime>& runtime,
    const std::unordered_set<std::uint64_t>& allowed
) {
    if (!world.has_resource(runtime->state_resource)) {
        return {};
    }
    const auto& current = storage<LuauStateStorage>(
        static_cast<const World&>(world),
        runtime->state_resource
    );
    auto current_variant = state_variant(current.current.ref(), state.type_id);
    if (!current_variant || !allowed.contains(*current_variant)) {
        return failure(
            luau_state_error(state, "reload removes the currently active value")
        );
    }
    if (!world.has_resource(runtime->next_state_resource)) {
        return {};
    }
    const auto& next = storage<LuauNextStateStorage>(
        static_cast<const World&>(world),
        runtime->next_state_resource
    );
    if (next.pending) {
        auto pending = state_variant(next.pending->ref(), state.type_id);
        if (!pending || !allowed.contains(*pending)) {
            return failure(
                luau_state_error(state, "reload removes the pending next value")
            );
        }
    }
    return {};
}

Status<LuauScriptError> install_luau_state_impl(
    World& world,
    const LuauStateDecl& state,
    Val initial,
    bool init_if_missing
) {
    auto ensured = ensure_luau_state_type(state);
    if (!ensured) {
        return ensured;
    }
    auto runtime = ensure_luau_state_runtime(state);
    std::unordered_set<std::uint64_t> allowed;
    for (const auto& value : state.values) {
        allowed.insert(value.id);
    }
    if (initial.type_id() != state.type_id) {
        return failure(luau_state_error(state, "initial value type mismatch"));
    }
    auto initial_variant = state_variant(initial.ref(), state.type_id);
    if (!initial_variant || !allowed.contains(*initial_variant)) {
        return failure(luau_state_error(state, "initial value is not allowed"));
    }
    if (init_if_missing) {
        auto valid = validate_existing_state(world, state, runtime, allowed);
        if (!valid) {
            return valid;
        }
    }

    if (!init_if_missing || !world.has_resource(runtime->state_resource)) {
        world.add_keyed_resource(
            runtime->state_resource,
            LuauStateStorage {.current = std::move(initial)}
        );
    }
    if (!init_if_missing || !world.has_resource(runtime->next_state_resource)) {
        world.add_keyed_resource(
            runtime->next_state_resource,
            LuauNextStateStorage {}
        );
    }
    const bool install_transition_runtime =
        !world.has_resource(runtime->transition_resource);
    if (install_transition_runtime) {
        world.add_keyed_resource(
            runtime->transition_resource,
            LuauStateTransitionStorage {}
        );
    } else if (!init_if_missing) {
        storage<LuauStateTransitionStorage>(
            world,
            runtime->transition_resource
        ) = LuauStateTransitionStorage {};
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

Status<LuauScriptError> ensure_luau_state_type(const LuauStateDecl& state) {
    auto ensured = ensure_luau_enum_type(
        LuauEnumDecl {
            .name = state.name,
            .qualified_name = state.qualified_name,
            .type_id = state.type_id,
            .values = state.values,
        }
    );
    if (!ensured) {
        return ensured;
    }
    ensure_luau_state_runtime(state);
    return {};
}

Status<LuauScriptError> ensure_luau_enum_type(const LuauEnumDecl& enumeration) {
    auto& registry = Registry::instance();
    registry.register_type<std::uint64_t>();
    if (auto existing = registry.try_get_type(enumeration.type_id)) {
        if (state_value_layout(enumeration.type_id) == nullptr) {
            return failure(
                LuauScriptError {
                    "Script enum '" + enumeration.qualified_name +
                        "': type id conflicts with existing type '" +
                        existing->name() + "'",
                }
            );
        }
        return {};
    }
    auto registered = registry.register_dynamic_struct(
        DynamicStructDesc {
            .name = enumeration.qualified_name,
            .id = enumeration.type_id,
            .fields = {
                DynamicFieldDesc {
                    .name = std::string {c_variant_field},
                    .type = type_id<std::uint64_t>(),
                },
            },
        }
    );
    if (!registered) {
        return failure(
            LuauScriptError {
                "Script enum '" + enumeration.qualified_name +
                    "': " + registered.error().message,
            }
        );
    }
    return {};
}

Result<Val, LuauScriptError> make_luau_enum_value(
    const LuauEnumDecl& enumeration,
    std::string_view value_name
) {
    const auto found = std::ranges::find(
        enumeration.values,
        value_name,
        &LuauEnumValueDecl::name
    );
    if (found == enumeration.values.end()) {
        return failure(
            LuauScriptError {
                "Script enum '" + enumeration.qualified_name +
                    "': has no value '" + std::string {value_name} + "'",
            }
        );
    }
    return make_state_value(
        enumeration.type_id,
        found->id,
        enumeration.qualified_name
    );
}

Result<Val, LuauScriptError>
make_luau_state_value(const LuauStateDecl& state, std::string_view value_name) {
    const auto found =
        std::ranges::find(state.values, value_name, &LuauEnumValueDecl::name);
    if (found == state.values.end()) {
        return failure(luau_state_error(
            state,
            "has no value '" + std::string {value_name} + "'"
        ));
    }
    return make_state_value(state.type_id, found->id, state.qualified_name);
}

Status<LuauScriptError> install_luau_state(
    World& world,
    const LuauStateDecl& state,
    Val initial,
    bool init_if_missing
) {
    return install_luau_state_impl(
        world,
        state,
        std::move(initial),
        init_if_missing
    );
}

bool is_luau_state_type(TypeId type) {
    return find_luau_state_runtime(type) != nullptr;
}

} // namespace ets
