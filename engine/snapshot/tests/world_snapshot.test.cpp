#include "snapshot/world_snapshot.hpp"

#include "ecs/commands.hpp"
#include "ecs/dynamic/events.hpp"
#include "ecs/dynamic/system.hpp"
#include "ecs/event.hpp"
#include "ecs/hierarchy.hpp"
#include "ecs/query.hpp"
#include "ecs/removed_components.hpp"
#include "ecs/system_params.hpp"
#include "ecs/world.hpp"
#include "refl/cls.hpp"
#include "refl/dynamic_type.hpp"
#include "refl/registry.hpp"
#include "refl/val.hpp"
#include "snapshot/events.hpp"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

using namespace fei;

namespace {

struct Position {
    int x {};
};

struct Health {
    int value {};
};

struct Hunter {
    Entity target;
};

struct GameState {
    int turn {};
    int score {};
    Entity player;
    Entity enemy;
};

struct RuntimeHandle {
    std::uintptr_t value {};
};

struct DerivedValue {
    int value {};
};

struct EncodedValue {
    int value {};
};

struct RoutedEvent {
    Entity target;
    std::vector<Entity> route;
    Optional<Entity> fallback;
    EncodedValue payload;
};

struct RoutedEventState {
    int total {};
};

struct ExternalServiceState {
    int value {};
};

struct DeterministicRuntimeState {
    std::uint64_t tick {};
    std::uint64_t rng {0x9e3779b97f4a7c15ULL};
    std::vector<int> pending_inputs;
    std::vector<int> pending_events;
};

struct BranchSummary {
    int turn {};
    int score {};
    int player_x {};
    int player_health {};
    bool enemy_alive {};
    int enemy_health {};

    bool operator==(const BranchSummary&) const = default;
};

struct ScheduledRetryPosition {
    int value {};
};

struct ScheduledRetryMarker {
    int value {};
};

struct ScheduledRetryEvent {
    int value {};
};

struct ScheduledRetryState {
    int input_delta {};
    bool spawn_requested {false};
    int changed_observations {};
    int added_observations {};
    int event_total {};
    int runs {};

    bool operator==(const ScheduledRetryState&) const = default;
};

struct ScheduledRetrySummary {
    int position {};
    std::size_t markers {};
    ScheduledRetryState state;

    bool operator==(const ScheduledRetrySummary&) const = default;
};

struct RemovalRetryState {
    int removals {};
    Entity last_entity {};
};

struct InSystemRestoreControl {
    bool requested {false};
    bool succeeded {false};
    std::size_t changed {};
};

constexpr ScheduleId ScheduledRetrySchedule = 0x5a17;
constexpr ScheduleId CallableRetrySchedule = 0x5a18;
constexpr ScheduleId DynamicRetrySchedule = 0x5a19;

class OpaqueDynamicExecutor final : public DynamicSystemExecutor {
  public:
    Status<DynamicSystemError> execute(const std::vector<Ref>&) override {
        return {};
    }
};

class CheckpointedDynamicExecutor final : public DynamicSystemExecutor {
  private:
    std::vector<int>* m_trace {nullptr};
    bool* m_fail_zero_restore {nullptr};
    int m_calls {};

  public:
    CheckpointedDynamicExecutor(
        std::vector<int>& trace,
        bool& fail_zero_restore
    ) : m_trace(&trace), m_fail_zero_restore(&fail_zero_restore) {}

    Status<DynamicSystemError> execute(const std::vector<Ref>&) override {
        m_trace->push_back(++m_calls);
        return {};
    }

    Result<SystemExecutorRuntimeState, RuntimeStateError>
    capture_runtime_state() const override {
        return SystemExecutorRuntimeState::from_value(m_calls);
    }

    Status<RuntimeStateError> validate_runtime_state(
        const SystemExecutorRuntimeState& state
    ) const override {
        if (state.kind != SystemExecutorRuntimeStateKind::Value ||
            state.value.try_get<int>() == nullptr) {
            return failure(
                RuntimeStateError {
                    .message = "Expected dynamic counter checkpoint state",
                }
            );
        }
        return {};
    }

    Status<RuntimeStateError>
    restore_runtime_state(const SystemExecutorRuntimeState& state) override {
        auto valid = validate_runtime_state(state);
        if (!valid) {
            return valid;
        }
        const auto value = *state.value.try_get<int>();
        if (*m_fail_zero_restore && value == 0) {
            return failure(
                RuntimeStateError {
                    .message = "Rejected zero dynamic counter state",
                }
            );
        }
        m_calls = value;
        return {};
    }
};

void apply_scheduled_retry_input(
    Query<ScheduledRetryPosition> positions,
    ResRW<ScheduledRetryState> state,
    Commands commands
) {
    if (state->input_delta != 0) {
        for (auto [position] : positions) {
            position->value += state->input_delta;
        }
    }
    if (state->spawn_requested) {
        commands.spawn().add(ScheduledRetryMarker {.value = state->runs});
    }
    state->input_delta = 0;
    state->spawn_requested = false;
}

void register_game_types();
serialization::ValueCodec encoded_value_codec();

TEST_CASE(
    "Snapshot restores dynamic event generations and remaps payload entities",
    "[snapshot][dynamic_events]"
) {
    register_game_types();
    World world;
    const auto first = world.entity();
    const auto target = world.entity();
    world.add_component(first, Position {.x = 1});
    world.add_component(target, Position {.x = 2});
    auto& events = world.add_resource(DynamicEvents {});
    events.send(
        type_id<RoutedEvent>(),
        make_val<RoutedEvent>(RoutedEvent {
            .target = target,
            .route = {first, target},
            .fallback = first,
            .payload = {.value = 7},
        })
    );
    events.update();
    events.send(
        type_id<RoutedEvent>(),
        make_val<RoutedEvent>(RoutedEvent {
            .target = first,
            .route = {target},
            .fallback = target,
            .payload = {.value = 11},
        })
    );

    snapshot::CheckpointStore checkpoints;
    checkpoints.registry().resource<DynamicEvents>(
        snapshot::ResourcePolicy::Snapshot
    );
    REQUIRE(checkpoints.registry().codecs().register_codec<EncodedValue>(
        encoded_value_codec()
    ));
    const auto coverage = snapshot::audit(world, checkpoints.registry());
    const auto dynamic_events_entry = std::ranges::find_if(
        coverage.resources,
        [](const snapshot::SnapshotAuditEntry& entry) {
            return entry.type == type_id<DynamicEvents>();
        }
    );
    REQUIRE(dynamic_events_entry != coverage.resources.end());
    CHECK(
        dynamic_events_entry->disposition ==
        snapshot::AuditDisposition::Snapshot
    );
    CHECK(dynamic_events_entry->serializable);
    REQUIRE(checkpoints.create("dynamic-events", world));
    world.resource<DynamicEvents>() = DynamicEvents {};

    auto restored = checkpoints.restore("dynamic-events", world);
    REQUIRE(restored);
    const auto* channel =
        world.resource<DynamicEvents>().channel(type_id<RoutedEvent>());
    REQUIRE(channel != nullptr);
    REQUIRE(channel->event_count == 2);
    REQUIRE(channel->previous.start_event_count == 0);
    REQUIRE(channel->previous.events.size() == 1);
    REQUIRE(channel->current.start_event_count == 1);
    REQUIRE(channel->current.events.size() == 1);

    const auto& previous =
        channel->previous.events[0].ref().get_const<RoutedEvent>();
    const auto& current =
        channel->current.events[0].ref().get_const<RoutedEvent>();
    REQUIRE(world.has_entity(previous.target));
    REQUIRE(world.has_entity(current.target));
    REQUIRE(previous.payload.value == 7);
    REQUIRE(current.payload.value == 11);
    REQUIRE(previous.route.size() == 2);
    REQUIRE(previous.route[0] == *previous.fallback);
    REQUIRE(current.route[0] == previous.target);
}

TEST_CASE(
    "Dynamic event snapshots respect ignored resource policy",
    "[snapshot][dynamic_events][policy]"
) {
    register_game_types();
    World world;
    auto& events = world.add_resource(DynamicEvents {});
    events.send(
        type_id<RoutedEvent>(),
        make_val<RoutedEvent>(RoutedEvent {.payload = {.value = 7}})
    );

    snapshot::SnapshotRegistry registry;
    registry.resource<DynamicEvents>(snapshot::ResourcePolicy::Ignore);
    const auto coverage = snapshot::audit(world, registry);
    REQUIRE(coverage.complete);
    REQUIRE(coverage.ready);
    auto checkpoint = snapshot::capture(world, registry);
    REQUIRE(checkpoint);
    CHECK_FALSE(checkpoint->dynamic_events_present);

    events.send(
        type_id<RoutedEvent>(),
        make_val<RoutedEvent>(RoutedEvent {.payload = {.value = 11}})
    );
    REQUIRE(snapshot::restore(world, *checkpoint, registry));
    const auto* channel =
        world.resource<DynamicEvents>().channel(type_id<RoutedEvent>());
    REQUIRE(channel != nullptr);
    CHECK(channel->event_count == 2);
}

void observe_scheduled_retry(
    Query<const ScheduledRetryPosition>::Filter<Changed<ScheduledRetryPosition>>
        changed,
    EventReader<ScheduledRetryEvent> events,
    ResRW<ScheduledRetryState> state
) {
    state->changed_observations += static_cast<int>(changed.size());
    if (auto event = events.next()) {
        state->event_total += event->value;
    }
    ++state->runs;
}

void observe_scheduled_retry_additions(
    Query<const ScheduledRetryMarker>::Filter<Added<ScheduledRetryMarker>>
        added,
    ResRW<ScheduledRetryState> state
) {
    state->added_observations += static_cast<int>(added.size());
}

void observe_retry_removals(
    RemovedComponents<ScheduledRetryPosition> removed,
    ResRW<RemovalRetryState> state
) {
    while (auto entity = removed.next()) {
        ++state->removals;
        state->last_entity = *entity;
    }
}

void observe_routed_events(
    EventReader<RoutedEvent> events,
    ResRW<RoutedEventState> state
) {
    while (auto event = events.next()) {
        state->total += event->payload.value;
    }
}

void restore_checkpoint_inside_system(
    Query<const ScheduledRetryPosition>::Filter<Changed<ScheduledRetryPosition>>
        changed,
    WorldRef world,
    ResRW<InSystemRestoreControl> control
) {
    control->changed = changed.size();
    if (!control->requested) {
        return;
    }
    control->requested = false;
    auto& checkpoints = world->resource<snapshot::CheckpointStore>();
    control->succeeded = checkpoints.restore("inside", *world).has_value();
}

ScheduledRetrySummary summarize_scheduled_retry(const World& world) {
    ScheduledRetrySummary summary {
        .state = world.resource<ScheduledRetryState>(),
    };
    for (const auto& [_, archetype] : world.archetypes()) {
        if (archetype.has_component(type_id<ScheduledRetryPosition>())) {
            for (std::size_t row = 0; row < archetype.size(); ++row) {
                summary.position +=
                    archetype
                        .get_component(type_id<ScheduledRetryPosition>(), row)
                        .get_const<ScheduledRetryPosition>()
                        .value;
            }
        }
        if (archetype.has_component(type_id<ScheduledRetryMarker>())) {
            summary.markers += archetype.size();
        }
    }
    return summary;
}

serialization::ValueCodec encoded_value_codec() {
    return serialization::ValueCodec {
        .encode =
            [](Ref value, std::string_view) -> Result<
                                                serialization::SerializedNode,
                                                serialization::SerializeError> {
            return serialization::SerializedNode::string(
                "score:" + std::to_string(value.get_const<EncodedValue>().value)
            );
        },
        .decode = [](const serialization::SerializedNode& node,
                     std::string_view path)
            -> Result<Val, serialization::DeserializeError> {
            const auto* text = node.try_string();
            constexpr std::string_view prefix {"score:"};
            if (text == nullptr || !text->starts_with(prefix)) {
                return failure(
                    serialization::DeserializeError {
                        .kind =
                            serialization::DeserializeError::Kind::InvalidNode,
                        .type = type_id<EncodedValue>(),
                        .path = std::string(path),
                        .message = "Expected an encoded score",
                    }
                );
            }
            return make_val<EncodedValue>(EncodedValue {
                .value = std::stoi(text->substr(prefix.size())),
            });
        },
    };
}

void register_game_types() {
    static bool registered = false;
    if (registered) {
        return;
    }

    auto& registry = Registry::instance();
    registry.register_cls<Position>({"snapshot_test"}, "Position")
        .add_property("x", &Position::x);
    registry.register_cls<Health>({"snapshot_test"}, "Health")
        .add_property("value", &Health::value);
    registry.register_cls<Hunter>({"snapshot_test"}, "Hunter")
        .add_property("target", &Hunter::target);
    registry.register_cls<GameState>({"snapshot_test"}, "GameState")
        .add_property("turn", &GameState::turn)
        .add_property("score", &GameState::score)
        .add_property("player", &GameState::player)
        .add_property("enemy", &GameState::enemy);
    registry
        .register_cls<DeterministicRuntimeState>(
            {"snapshot_test"},
            "DeterministicRuntimeState"
        )
        .add_property("tick", &DeterministicRuntimeState::tick)
        .add_property("rng", &DeterministicRuntimeState::rng)
        .add_property(
            "pending_inputs",
            &DeterministicRuntimeState::pending_inputs
        )
        .add_property(
            "pending_events",
            &DeterministicRuntimeState::pending_events
        );
    registry
        .register_cls<ScheduledRetryPosition>(
            {"snapshot_test"},
            "ScheduledRetryPosition"
        )
        .add_property("value", &ScheduledRetryPosition::value);
    registry
        .register_cls<ScheduledRetryMarker>(
            {"snapshot_test"},
            "ScheduledRetryMarker"
        )
        .add_property("value", &ScheduledRetryMarker::value);
    registry
        .register_cls<ScheduledRetryEvent>(
            {"snapshot_test"},
            "ScheduledRetryEvent"
        )
        .add_property("value", &ScheduledRetryEvent::value);
    registry.register_cls<RoutedEvent>({"snapshot_test"}, "RoutedEvent")
        .add_property("target", &RoutedEvent::target)
        .add_property("route", &RoutedEvent::route)
        .add_property("fallback", &RoutedEvent::fallback)
        .add_property("payload", &RoutedEvent::payload);
    registry
        .register_cls<RoutedEventState>({"snapshot_test"}, "RoutedEventState")
        .add_property("total", &RoutedEventState::total);
    registry
        .register_cls<ScheduledRetryState>(
            {"snapshot_test"},
            "ScheduledRetryState"
        )
        .add_property("input_delta", &ScheduledRetryState::input_delta)
        .add_property("spawn_requested", &ScheduledRetryState::spawn_requested)
        .add_property(
            "changed_observations",
            &ScheduledRetryState::changed_observations
        )
        .add_property(
            "added_observations",
            &ScheduledRetryState::added_observations
        )
        .add_property("event_total", &ScheduledRetryState::event_total)
        .add_property("runs", &ScheduledRetryState::runs);
    registry
        .register_cls<RemovalRetryState>({"snapshot_test"}, "RemovalRetryState")
        .add_property("removals", &RemovalRetryState::removals)
        .add_property("last_entity", &RemovalRetryState::last_entity);
    registered = true;
}

World make_game() {
    World world;
    const auto player = world.entity();
    world.add_component(player, Position {.x = 0});
    world.add_component(player, Health {.value = 10});

    const auto enemy = world.entity();
    world.add_component(enemy, Position {.x = 2});
    world.add_component(enemy, Health {.value = 7});
    world.add_component(enemy, Hunter {.target = player});

    world.add_resource(
        GameState {
            .turn = 0,
            .score = 0,
            .player = player,
            .enemy = enemy,
        }
    );
    return world;
}

snapshot::ResourceRegistry game_resources() {
    snapshot::ResourceRegistry resources;
    resources.include<GameState>();
    return resources;
}

void attack(World& world) {
    auto& state = world.resource<GameState>();
    ++state.turn;
    const auto player = state.player;
    const auto enemy = state.enemy;
    world.get_component_rw<Health>(player)->value -= 2;
    auto enemy_health = world.get_component_rw<Health>(enemy);
    enemy_health->value -= 4;
    if (enemy_health->value <= 0) {
        world.despawn(enemy);
        state.score += 10;
    }
}

void retreat(World& world) {
    auto& state = world.resource<GameState>();
    ++state.turn;
    world.get_component_rw<Position>(state.player)->x -= 1;
}

BranchSummary summarize(const World& world) {
    const auto& state = world.resource<GameState>();
    const auto enemy_alive = world.has_entity(state.enemy);
    return BranchSummary {
        .turn = state.turn,
        .score = state.score,
        .player_x = world.get_component<Position>(state.player).x,
        .player_health = world.get_component<Health>(state.player).value,
        .enemy_alive = enemy_alive,
        .enemy_health =
            enemy_alive ? world.get_component<Health>(state.enemy).value : 0,
    };
}

} // namespace

TEST_CASE(
    "World snapshot restores a game with remapped entity references",
    "[snapshot][ecs]"
) {
    register_game_types();
    auto world = make_game();
    const auto original_player = world.resource<GameState>().player;
    const auto original_enemy = world.resource<GameState>().enemy;

    auto checkpoint = snapshot::capture(world, game_resources());
    REQUIRE(checkpoint);

    attack(world);
    attack(world);
    const auto discarded_entity = world.entity();
    world.add_component(discarded_entity, Position {.x = 99});
    REQUIRE_FALSE(world.has_entity(original_enemy));

    auto restored = snapshot::restore(world, *checkpoint);
    REQUIRE(restored);
    REQUIRE_FALSE(world.has_entity(original_player));
    REQUIRE_FALSE(world.has_entity(discarded_entity));

    const auto& state = world.resource<GameState>();
    REQUIRE(state.player != original_player);
    REQUIRE(state.enemy != original_enemy);
    REQUIRE(world.has_entity(state.player));
    REQUIRE(world.has_entity(state.enemy));
    REQUIRE(world.get_component<Hunter>(state.enemy).target == state.player);
    REQUIRE(summarize(world) == BranchSummary {0, 0, 0, 10, true, 7});
}

TEST_CASE(
    "World snapshot supports deterministic retry and alternate branches",
    "[snapshot][retry]"
) {
    register_game_types();
    auto world = make_game();
    auto checkpoint = snapshot::capture(world, game_resources());
    REQUIRE(checkpoint);

    attack(world);
    attack(world);
    const auto first_attack_branch = summarize(world);

    REQUIRE(snapshot::restore(world, *checkpoint));
    attack(world);
    attack(world);
    REQUIRE(summarize(world) == first_attack_branch);

    REQUIRE(snapshot::restore(world, *checkpoint));
    retreat(world);
    REQUIRE(summarize(world) != first_attack_branch);
}

TEST_CASE(
    "World snapshot deterministically retries scheduled runtime state",
    "[snapshot][retry][schedule][events][change_detection][commands]"
) {
    register_game_types();
    Registry::instance().register_type<CommandsQueue>();

    World world;
    world.add_resource(CommandsQueue {});
    world.add_resource(Events<ScheduledRetryEvent> {});
    world.add_resource(ScheduledRetryState {});
    const auto player = world.entity();
    world.add_component(player, ScheduledRetryPosition {});
    world.add_systems(
        ScheduledRetrySchedule,
        chain(
            apply_scheduled_retry_input,
            observe_scheduled_retry,
            observe_scheduled_retry_additions
        )
    );
    world.sort_systems();

    auto& events = world.resource<Events<ScheduledRetryEvent>>();
    events.send(ScheduledRetryEvent {.value = 10});
    events.send(ScheduledRetryEvent {.value = 20});
    world.run_schedule(ScheduledRetrySchedule);

    snapshot::CheckpointStore checkpoints;
    checkpoints.resources().include<ScheduledRetryState>();
    REQUIRE(
        snapshot::register_event_resource<ScheduledRetryEvent>(
            checkpoints.registry()
        )
    );
    checkpoints.registry().resource<CommandsQueue>(
        snapshot::ResourcePolicy::Ignore
    );
    REQUIRE(checkpoints.create("scheduled", world));

    auto run_branch = [&] {
        auto& state = world.resource<ScheduledRetryState>();
        state.input_delta = 3;
        state.spawn_requested = true;
        world.run_schedule(ScheduledRetrySchedule);
        return summarize_scheduled_retry(world);
    };

    const auto first = run_branch();
    REQUIRE(first.position == 3);
    REQUIRE(first.markers == 1);
    REQUIRE(first.state.changed_observations == 2);
    REQUIRE(first.state.added_observations == 1);
    REQUIRE(first.state.event_total == 30);

    world.resource<Events<ScheduledRetryEvent>>().update();
    world.resource<Events<ScheduledRetryEvent>>().update();
    REQUIRE(world.resource<Events<ScheduledRetryEvent>>().size() == 0);

    REQUIRE(checkpoints.restore("scheduled", world));
    const auto& restored_events = static_cast<const World&>(world)
                                      .resource<Events<ScheduledRetryEvent>>();
    const auto restored_event = restored_events.get_event(0);
    REQUIRE(restored_event);
    CHECK(restored_event->id.events == &restored_events);
    CHECK(run_branch() == first);
}

TEST_CASE(
    "World snapshot retries pending removed-component events",
    "[snapshot][retry][removed_components]"
) {
    register_game_types();
    Registry::instance().register_type<CommandsQueue>();

    World world;
    world.add_resource(CommandsQueue {});
    world.add_resource(RemovalRetryState {});
    const auto entity = world.entity();
    world.add_component(entity, ScheduledRetryPosition {});
    world.add_systems(ScheduledRetrySchedule, observe_retry_removals);
    world.run_schedule(ScheduledRetrySchedule);
    world.remove_component<ScheduledRetryPosition>(entity);

    snapshot::CheckpointStore checkpoints;
    checkpoints.resources().include<RemovalRetryState>();
    checkpoints.registry().resource<CommandsQueue>(
        snapshot::ResourcePolicy::Ignore
    );
    REQUIRE(checkpoints.create("removed", world));
    world.run_schedule(ScheduledRetrySchedule);
    REQUIRE(world.resource<RemovalRetryState>().removals == 1);
    REQUIRE(world.resource<RemovalRetryState>().last_entity == entity);
    REQUIRE(world.has_entity(entity));
    REQUIRE_FALSE(world.has_component<ScheduledRetryPosition>(entity));

    const auto restored = checkpoints.restore("removed", world);
    REQUIRE(restored);
    world.run_schedule(ScheduledRetrySchedule);
    const auto restored_entity = restored->entity(1);
    CHECK(world.resource<RemovalRetryState>().removals == 1);
    CHECK(world.resource<RemovalRetryState>().last_entity == restored_entity);
    CHECK(world.has_entity(restored_entity));
    CHECK_FALSE(world.has_component<ScheduledRetryPosition>(restored_entity));
}

TEST_CASE(
    "Event payload codecs inherit snapshot entity remapping",
    "[snapshot][event][codec][entity_reference]"
) {
    register_game_types();
    Registry::instance().register_type<CommandsQueue>();

    World world;
    world.add_resource(CommandsQueue {});
    world.add_resource(Events<RoutedEvent> {});
    world.add_resource(RoutedEventState {});
    const auto player = world.entity();
    const auto ally = world.entity();
    world.add_component(player, Position {.x = 1});
    world.add_component(ally, Position {.x = 2});
    world.add_systems(ScheduledRetrySchedule, observe_routed_events);

    world.resource<Events<RoutedEvent>>().send(
        RoutedEvent {
            .target = player,
            .route = {ally},
            .fallback = ally,
            .payload = EncodedValue {.value = 7},
        }
    );
    world.resource<Events<RoutedEvent>>().update();
    world.run_schedule(ScheduledRetrySchedule);
    REQUIRE(world.resource<RoutedEventState>().total == 7);
    world.resource<Events<RoutedEvent>>().send(
        RoutedEvent {
            .target = ally,
            .route =
                {
                    player,
                    ally,
                },
            .fallback = player,
            .payload = EncodedValue {.value = 11},
        }
    );

    auto source = std::make_unique<snapshot::CheckpointStore>();
    source->registry().resources().include<RoutedEventState>();
    source->registry().resource<CommandsQueue>(
        snapshot::ResourcePolicy::Ignore
    );
    REQUIRE(source->registry().codecs().register_codec<EncodedValue>(
        encoded_value_codec()
    ));
    REQUIRE(snapshot::register_event_resource<RoutedEvent>(source->registry()));
    REQUIRE(source->create("routed", world));

    snapshot::CheckpointStore checkpoints = std::move(*source);
    source.reset();

    world.run_schedule(ScheduledRetrySchedule);
    REQUIRE(world.resource<RoutedEventState>().total == 18);
    world.despawn(player);
    world.despawn(ally);
    REQUIRE_FALSE(world.has_entity(player));
    REQUIRE_FALSE(world.has_entity(ally));
    world.resource<Events<RoutedEvent>>().clear();

    const auto restored = checkpoints.restore("routed", world);
    REQUIRE(restored);
    const auto restored_player = restored->entity(1);
    const auto restored_ally = restored->entity(2);
    REQUIRE(restored_player != player);
    REQUIRE(restored_ally != ally);

    const auto& events = world.resource<Events<RoutedEvent>>();
    REQUIRE(events.previous_sequence().events.size() == 1);
    REQUIRE(events.current_sequence().events.size() == 1);
    const auto& previous = events.previous_sequence().events.front();
    const auto& current = events.current_sequence().events.front();
    CHECK(previous.id.events == &events);
    CHECK(current.id.events == &events);
    CHECK(previous.event.target == restored_player);
    REQUIRE(previous.event.route.size() == 1);
    CHECK(previous.event.route.front() == restored_ally);
    REQUIRE(previous.event.fallback);
    CHECK(*previous.event.fallback == restored_ally);
    CHECK(previous.event.payload.value == 7);
    CHECK(current.event.target == restored_ally);
    REQUIRE(current.event.route.size() == 2);
    CHECK(current.event.route[0] == restored_player);
    CHECK(current.event.route[1] == restored_ally);
    REQUIRE(current.event.fallback);
    CHECK(*current.event.fallback == restored_player);
    CHECK(current.event.payload.value == 11);

    world.run_schedule(ScheduledRetrySchedule);
    CHECK(world.resource<RoutedEventState>().total == 18);
}

TEST_CASE(
    "Event payload rejects nested references outside the captured world",
    "[snapshot][event][entity_reference][validation]"
) {
    register_game_types();
    World world;
    world.add_resource(Events<RoutedEvent> {});
    const auto entity = world.entity();
    world.add_component(entity, Position {});
    world.resource<Events<RoutedEvent>>().send(
        RoutedEvent {
            .target = entity,
            .route = {Entity {999999}},
            .payload = EncodedValue {.value = 3},
        }
    );

    snapshot::CheckpointStore checkpoints;
    REQUIRE(checkpoints.registry().codecs().register_codec<EncodedValue>(
        encoded_value_codec()
    ));
    REQUIRE(
        snapshot::register_event_resource<RoutedEvent>(checkpoints.registry())
    );
    const auto created = checkpoints.create("external_event", world);
    REQUIRE_FALSE(created);
    CHECK(
        created.error().kind ==
        snapshot::SnapshotError::Kind::InvalidEntityReference
    );
    CHECK(created.error().path.find("current[0]") != std::string::npos);
    CHECK(created.error().path.find("route") != std::string::npos);
}

TEST_CASE(
    "World snapshot rejects unsafe command and schedule boundaries",
    "[snapshot][checkpoint][boundary][schedule]"
) {
    register_game_types();
    Registry::instance().register_type<CommandsQueue>();

    World world;
    world.add_resource(CommandsQueue {});
    world.add_resource(ScheduledRetryState {});
    snapshot::CheckpointStore checkpoints;
    checkpoints.resources().include<ScheduledRetryState>();
    checkpoints.registry().resource<CommandsQueue>(
        snapshot::ResourcePolicy::Ignore
    );

    world.resource<CommandsQueue>().add_command([](World&) {
    });
    const auto unsafe_audit = snapshot::audit(world, checkpoints.registry());
    CHECK_FALSE(unsafe_audit.ready);
    CHECK_FALSE(unsafe_audit.runtime_ready);
    CHECK(
        unsafe_audit.runtime_message.find("CommandsQueue") != std::string::npos
    );
    const auto pending = checkpoints.create("pending", world);
    REQUIRE_FALSE(pending);
    CHECK(
        pending.error().kind ==
        snapshot::SnapshotError::Kind::CheckpointBoundaryFailed
    );
    world.apply_deferred();

    REQUIRE(checkpoints.create("topology", world));
    world.add_systems(ScheduledRetrySchedule, [](ResRO<ScheduledRetryState>) {
    });
    const auto changed = checkpoints.restore("topology", world);
    REQUIRE_FALSE(changed);
    CHECK(
        changed.error().kind ==
        snapshot::SnapshotError::Kind::RuntimeStateFailed
    );
}

TEST_CASE(
    "World snapshot requires explicit callable checkpoint adapters",
    "[snapshot][checkpoint][system][condition]"
) {
    SECTION("opaque captured callable is rejected") {
        World world;
        int calls = 0;
        world.add_systems(CallableRetrySchedule, [&calls]() {
            ++calls;
        });

        snapshot::CheckpointStore checkpoints;
        const auto audit = snapshot::audit(world, checkpoints.registry());
        CHECK_FALSE(audit.ready);
        CHECK_FALSE(audit.runtime_ready);
        CHECK(
            audit.runtime_message.find("checkpointed_system") !=
            std::string::npos
        );
        const auto created = checkpoints.create("opaque", world);
        REQUIRE_FALSE(created);
        CHECK(
            created.error().kind ==
            snapshot::SnapshotError::Kind::RuntimeStateFailed
        );
    }

    SECTION("copy adapters rewind mutable system and condition state") {
        Registry::instance().register_type<CommandsQueue>();
        World world;
        world.add_resource(CommandsQueue {});
        std::vector<int> system_trace;
        std::vector<int> condition_trace;
        world.add_systems(
            CallableRetrySchedule,
            checkpointed_system(
                named_system(
                    "callable_retry",
                    [counter = 0, &system_trace]() mutable {
                        system_trace.push_back(++counter);
                    }
                )
            ).run_if_checkpointed([counter = 0, &condition_trace]() mutable {
                condition_trace.push_back(++counter);
                return true;
            })
        );

        snapshot::CheckpointStore checkpoints;
        checkpoints.registry().resource<CommandsQueue>(
            snapshot::ResourcePolicy::Ignore
        );
        REQUIRE(checkpoints.create("callable", world));
        world.run_schedule(CallableRetrySchedule);
        REQUIRE(checkpoints.restore("callable", world));
        world.run_schedule(CallableRetrySchedule);

        CHECK(system_trace == std::vector<int> {1, 1});
        CHECK(condition_trace == std::vector<int> {1, 1});
    }
}

TEST_CASE(
    "World snapshot restores dynamic executor state transactionally",
    "[snapshot][checkpoint][dynamic_system][transaction]"
) {
    SECTION("dynamic executors are opaque by default") {
        World world;
        world.add_systems(
            DynamicRetrySchedule,
            SystemConfig(
                std::make_unique<DynamicSystem>(
                    "opaque",
                    DynamicSystemParams {},
                    std::make_unique<OpaqueDynamicExecutor>()
                )
            )
        );

        snapshot::CheckpointStore checkpoints;
        const auto created = checkpoints.create("opaque_dynamic", world);
        REQUIRE_FALSE(created);
        CHECK(
            created.error().message.find("no checkpoint adapter") !=
            std::string::npos
        );
    }

    SECTION("custom adapter rewinds and rolls back failed restore") {
        Registry::instance().register_type<CommandsQueue>();
        World world;
        world.add_resource(CommandsQueue {});
        std::vector<int> trace;
        bool fail_zero_restore = false;
        world.add_systems(
            DynamicRetrySchedule,
            SystemConfig(
                std::make_unique<DynamicSystem>(
                    "counter",
                    DynamicSystemParams {},
                    std::make_unique<CheckpointedDynamicExecutor>(
                        trace,
                        fail_zero_restore
                    )
                )
            )
        );

        snapshot::CheckpointStore checkpoints;
        checkpoints.registry().resource<CommandsQueue>(
            snapshot::ResourcePolicy::Ignore
        );
        REQUIRE(checkpoints.create("dynamic", world));
        world.run_schedule(DynamicRetrySchedule);
        REQUIRE(checkpoints.restore("dynamic", world));
        world.run_schedule(DynamicRetrySchedule);
        CHECK(trace == std::vector<int> {1, 1});

        fail_zero_restore = true;
        const auto rejected = checkpoints.restore("dynamic", world);
        REQUIRE_FALSE(rejected);
        CHECK(
            rejected.error().kind ==
            snapshot::SnapshotError::Kind::RuntimeStateFailed
        );

        world.run_schedule(DynamicRetrySchedule);
        CHECK(trace == std::vector<int> {1, 1, 2});
    }
}

TEST_CASE(
    "World snapshot defers active system state restore until system exit",
    "[snapshot][retry][schedule][active_system]"
) {
    register_game_types();
    Registry::instance().register_type<CommandsQueue>();

    World world;
    world.add_resource(CommandsQueue {});
    world.add_resource(ScheduledRetryState {});
    world.add_resource(InSystemRestoreControl {});
    world.add_resource(snapshot::CheckpointStore {});
    const auto entity = world.entity();
    world.add_component(entity, ScheduledRetryPosition {});
    world.add_systems(ScheduledRetrySchedule, restore_checkpoint_inside_system);
    world.run_schedule(ScheduledRetrySchedule);

    auto& checkpoints = world.resource<snapshot::CheckpointStore>();
    checkpoints.resources().include<ScheduledRetryState>();
    checkpoints.registry().resource<CommandsQueue>(
        snapshot::ResourcePolicy::Ignore
    );
    checkpoints.registry().resource<InSystemRestoreControl>(
        snapshot::ResourcePolicy::Ignore
    );
    checkpoints.registry().resource<snapshot::CheckpointStore>(
        snapshot::ResourcePolicy::Ignore
    );
    REQUIRE(checkpoints.create("inside", world));

    world.get_component_rw<ScheduledRetryPosition>(entity)->value = 1;
    world.run_schedule(ScheduledRetrySchedule);
    REQUIRE(world.resource<InSystemRestoreControl>().changed == 1);

    world.resource<InSystemRestoreControl>().requested = true;
    world.run_schedule(ScheduledRetrySchedule);
    REQUIRE(world.resource<InSystemRestoreControl>().succeeded);

    const auto restored_entity =
        Query<Entity, const ScheduledRetryPosition>::get_param(
            world,
            SystemTicks {
                .last_run = 0,
                .this_run = world.read_change_tick(),
            }
        )
            .first();
    world
        .get_component_rw<ScheduledRetryPosition>(std::get<0>(restored_entity))
        ->value = 1;
    world.run_schedule(ScheduledRetrySchedule);
    CHECK(world.resource<InSystemRestoreControl>().changed == 1);
}

TEST_CASE(
    "Deterministic resources restore tick RNG input and event queues",
    "[snapshot][determinism][resource]"
) {
    register_game_types();
    World world;
    world.add_resource(
        DeterministicRuntimeState {
            .pending_inputs = {2, 1},
            .pending_events = {7},
        }
    );
    snapshot::ResourceRegistry resources;
    resources.include<DeterministicRuntimeState>();
    auto checkpoint = snapshot::capture(world, resources);
    REQUIRE(checkpoint);

    auto run_two_ticks = [&world] {
        std::vector<std::uint64_t> observations;
        for (int tick = 0; tick < 2; ++tick) {
            auto& state = world.resource<DeterministicRuntimeState>();
            ++state.tick;
            state.rng ^= state.rng >> 12U;
            state.rng ^= state.rng << 25U;
            state.rng ^= state.rng >> 27U;
            const auto input = state.pending_inputs.front();
            state.pending_inputs.erase(state.pending_inputs.begin());
            state.pending_events.push_back(input);
            observations.push_back(state.rng);
            observations.push_back(state.tick);
            observations.push_back(state.pending_events.size());
        }
        return observations;
    };

    const auto first_run = run_two_ticks();
    REQUIRE(snapshot::restore(world, *checkpoint));
    CHECK(run_two_ticks() == first_run);
}

TEST_CASE(
    "World snapshot rejects references outside the captured world",
    "[snapshot][validation]"
) {
    register_game_types();
    auto world = make_game();
    world.resource<GameState>().enemy = Entity {999999};

    const auto checkpoint = snapshot::capture(world, game_resources());
    REQUIRE_FALSE(checkpoint);
    REQUIRE(
        checkpoint.error().kind ==
        snapshot::SnapshotError::Kind::InvalidEntityReference
    );
}

TEST_CASE(
    "World snapshot accepts cleared dynamic optional entity references",
    "[snapshot][entity_reference][optional][dynamic]"
) {
    auto& registry = Registry::instance();
    registry.register_type<Optional<Entity>>();
    const TypeId state_type {
        std::string_view {"snapshot_test.DynamicOptionalEntityState"},
    };
    auto registered = registry.register_dynamic_struct(
        DynamicStructDesc {
            .name = "snapshot_test.DynamicOptionalEntityState",
            .id = state_type,
            .fields = {
                DynamicFieldDesc {
                    .name = "target",
                    .type = type_id<Optional<Entity>>(),
                },
            },
        }
    );
    REQUIRE(registered.has_value());

    World world;
    const Entity target = world.entity();
    Val state = Val::default_construct(*registered);
    auto& target_property = registry.get_cls(state_type).get_property("target");
    Val target_value = make_val<Optional<Entity>>(target);
    REQUIRE(target_property.set(state.ref(), target_value.ref()));
    world.add_resource(state_type, std::move(state));

    snapshot::CheckpointStore checkpoints;
    REQUIRE(checkpoints.create("target-present", world));

    Val cleared = make_val<Optional<Entity>>();
    REQUIRE(target_property.set(world.resource(state_type), cleared.ref()));
    world.despawn(target);
    REQUIRE(checkpoints.create("target-cleared", world));

    auto restored = checkpoints.restore("target-cleared", world);
    REQUIRE(restored.has_value());
    auto restored_target = target_property.get(world.resource(state_type));
    REQUIRE(restored_target.has_value());
    CHECK(restored_target->get_const<Optional<Entity>>() == nullopt);
}

TEST_CASE(
    "Snapshot registry ignores external data and rebuilds derived components",
    "[snapshot][policy][hook][codec]"
) {
    register_game_types();
    auto world = make_game();
    const auto player = world.resource<GameState>().player;
    world.add_component(player, RuntimeHandle {.value = 0x1234});
    world.add_component(player, DerivedValue {.value = -1});
    world.add_component(player, EncodedValue {.value = 42});

    const auto unsupported = snapshot::capture(world, game_resources());
    REQUIRE_FALSE(unsupported);

    snapshot::CheckpointStore checkpoints;
    auto& registry = checkpoints.registry();
    registry.resources().include<GameState>();
    registry.component<RuntimeHandle>(snapshot::ComponentPolicy::Ignore);
    registry.component<DerivedValue>(snapshot::ComponentPolicy::Rebuild);
    REQUIRE(
        registry.codecs().register_codec<EncodedValue>(encoded_value_codec())
    );

    const auto coverage = snapshot::audit(world, registry);
    REQUIRE(coverage.ready);
    const auto runtime_handle = std::ranges::find_if(
        coverage.components,
        [](const snapshot::SnapshotAuditEntry& entry) {
            return entry.type == type_id<RuntimeHandle>();
        }
    );
    const auto derived_value = std::ranges::find_if(
        coverage.components,
        [](const snapshot::SnapshotAuditEntry& entry) {
            return entry.type == type_id<DerivedValue>();
        }
    );
    REQUIRE(runtime_handle != coverage.components.end());
    REQUIRE(derived_value != coverage.components.end());
    CHECK(runtime_handle->disposition == snapshot::AuditDisposition::Ignore);
    CHECK(derived_value->disposition == snapshot::AuditDisposition::Rebuild);

    int before_restore_count = 0;
    int after_restore_count = 0;
    registry.on_before_restore(
        [&before_restore_count](World&) -> Status<snapshot::SnapshotError> {
            ++before_restore_count;
            return {};
        }
    );
    registry.on_after_restore(
        [&after_restore_count](
            World& restored_world
        ) -> Status<snapshot::SnapshotError> {
            ++after_restore_count;
            const auto restored_player =
                restored_world.resource<GameState>().player;
            const auto x =
                restored_world.get_component<Position>(restored_player).x;
            restored_world.add_component(
                restored_player,
                DerivedValue {.value = x + 100}
            );
            return {};
        }
    );

    REQUIRE(checkpoints.create("configured", world));
    world.get_component_rw<Position>(player)->x = 9;
    world.get_component_rw<EncodedValue>(player)->value = 0;
    REQUIRE(checkpoints.restore("configured", world));

    const auto restored_player = world.resource<GameState>().player;
    CHECK(before_restore_count == 1);
    CHECK(after_restore_count == 1);
    CHECK_FALSE(world.has_component<RuntimeHandle>(restored_player));
    REQUIRE(world.has_component<DerivedValue>(restored_player));
    CHECK(world.get_component<DerivedValue>(restored_player).value == 100);
    CHECK(world.get_component<EncodedValue>(restored_player).value == 42);
}

TEST_CASE(
    "Failed after-restore hook rolls back the exact previous entity graph",
    "[snapshot][transaction][hook]"
) {
    register_game_types();
    auto world = make_game();
    const auto original_player = world.resource<GameState>().player;
    world.add_component(original_player, RuntimeHandle {.value = 0x1234});
    world.add_resource(ExternalServiceState {.value = 7});

    snapshot::CheckpointStore checkpoints;
    checkpoints.registry().resources().include<GameState>();
    checkpoints.registry().component<RuntimeHandle>(
        snapshot::ComponentPolicy::Ignore
    );
    checkpoints.registry().resource<ExternalServiceState>(
        snapshot::ResourcePolicy::Rebuild
    );
    REQUIRE(checkpoints.create("before", world));

    world.get_component_rw<Position>(original_player)->x = 9;
    world.get_component_rw<RuntimeHandle>(original_player)->value = 0x9999;
    checkpoints.registry().on_after_restore(
        [](World& restored_world) -> Status<snapshot::SnapshotError> {
            restored_world.resource<ExternalServiceState>().value = 99;
            return failure(
                snapshot::SnapshotError {
                    .kind = snapshot::SnapshotError::Kind::RestoreHookFailed,
                    .path = "test",
                    .message = "rebuild failed",
                }
            );
        }
    );
    int rollback_count = 0;
    checkpoints.registry().on_restore_rollback(
        [&rollback_count](
            World& restored_world
        ) -> Status<snapshot::SnapshotError> {
            ++rollback_count;
            restored_world.resource<ExternalServiceState>().value = 7;
            return {};
        }
    );

    const auto restored = checkpoints.restore("before", world);
    REQUIRE_FALSE(restored);
    CHECK(
        restored.error().kind ==
        snapshot::SnapshotError::Kind::RestoreHookFailed
    );
    REQUIRE(world.has_entity(original_player));
    CHECK(world.resource<GameState>().player == original_player);
    CHECK(world.get_component<Position>(original_player).x == 9);
    CHECK(world.get_component<RuntimeHandle>(original_player).value == 0x9999);
    CHECK(rollback_count == 1);
    CHECK(world.resource<ExternalServiceState>().value == 7);
}

TEST_CASE(
    "Strict checkpoints require explicit policies for every resource",
    "[snapshot][audit][strict][resource]"
) {
    register_game_types();
    auto world = make_game();
    world.add_resource(ExternalServiceState {.value = 5});
    snapshot::CheckpointStore checkpoints;
    checkpoints.registry().resources().include<GameState>();

    const auto incomplete = snapshot::audit(world, checkpoints.registry());
    CHECK(incomplete.ready);
    CHECK_FALSE(incomplete.complete);
    const auto rejected = checkpoints.create("strict", world, true);
    REQUIRE_FALSE(rejected);
    CHECK(
        rejected.error().kind ==
        snapshot::SnapshotError::Kind::StrictAuditFailed
    );

    checkpoints.registry().resource<ExternalServiceState>(
        snapshot::ResourcePolicy::Ignore
    );
    const auto complete = snapshot::audit(world, checkpoints.registry());
    CHECK(complete.ready);
    CHECK(complete.complete);
    REQUIRE(checkpoints.create("strict", world, true));
}

TEST_CASE(
    "Checkpoint store evicts by revision and tracks its memory budget",
    "[snapshot][checkpoint][limits]"
) {
    register_game_types();
    World world;
    const auto entity = world.entity();
    world.add_component(entity, Position {.x = 1});
    snapshot::CheckpointStore checkpoints;
    checkpoints.set_limits(
        snapshot::CheckpointLimits {
            .max_count = 2,
            .max_bytes = 1024 * 1024,
            .eviction = snapshot::CheckpointEviction::Oldest,
        }
    );

    const auto first = checkpoints.create("first", world);
    REQUIRE(first);
    CHECK(first->byte_size > 0);
    world.get_component_rw<Position>(entity)->x = 2;
    REQUIRE(checkpoints.create("second", world));
    world.get_component_rw<Position>(entity)->x = 3;
    REQUIRE(checkpoints.create("third", world));
    const auto entries = checkpoints.list();
    REQUIRE(entries.size() == 2);
    CHECK(entries[0].name == "second");
    CHECK(entries[1].name == "third");
    CHECK(
        checkpoints.total_bytes() == entries[0].byte_size + entries[1].byte_size
    );

    CHECK(checkpoints.erase("second"));
    CHECK_FALSE(checkpoints.erase("missing"));
    CHECK(checkpoints.clear() == 1);
    CHECK(checkpoints.total_bytes() == 0);

    checkpoints.set_limits(
        snapshot::CheckpointLimits {
            .max_count = 1,
            .max_bytes = 1024 * 1024,
            .eviction = snapshot::CheckpointEviction::Reject,
        }
    );
    REQUIRE(checkpoints.create("kept", world));
    const auto full = checkpoints.create("rejected", world);
    REQUIRE_FALSE(full);
    CHECK(
        full.error().kind ==
        snapshot::SnapshotError::Kind::CheckpointLimitReached
    );
}

TEST_CASE(
    "World snapshot rebuilds ECS hierarchy from ChildOf relationships",
    "[snapshot][hierarchy]"
) {
    register_game_types();
    World world;
    const auto root = world.entity();
    const auto child = world.entity();
    const auto grandchild = world.entity();
    world.add_component(root, Position {.x = 1});
    world.add_component(child, Position {.x = 2});
    world.add_component(grandchild, Position {.x = 3});
    world.set_parent(child, root);
    world.set_parent(grandchild, child);
    const auto root_location = world.entity_location(root);
    REQUIRE(root_location);
    const auto root_children_ticks =
        world.archetypes()
            .get(root_location->archetype_id)
            .component_ticks(type_id<Children>(), root_location->row);

    auto checkpoint = snapshot::capture(world);
    REQUIRE(checkpoint);
    world.despawn(root);
    REQUIRE_FALSE(world.has_entity(root));
    REQUIRE_FALSE(world.has_entity(child));
    REQUIRE_FALSE(world.has_entity(grandchild));

    auto restored = snapshot::restore(world, *checkpoint);
    REQUIRE(restored);
    const auto restored_root = restored->entity(1);
    const auto restored_child = restored->entity(2);
    const auto restored_grandchild = restored->entity(3);
    REQUIRE(world.parent(restored_child));
    REQUIRE(world.parent(restored_grandchild));
    CHECK(*world.parent(restored_child) == restored_root);
    CHECK(*world.parent(restored_grandchild) == restored_child);
    REQUIRE(world.has_component<Children>(restored_root));
    REQUIRE(world.has_component<Children>(restored_child));
    const auto restored_root_location = world.entity_location(restored_root);
    REQUIRE(restored_root_location);
    CHECK(
        world.archetypes()
            .get(restored_root_location->archetype_id)
            .component_ticks(
                type_id<Children>(),
                restored_root_location->row
            ) == root_children_ticks
    );
    CHECK(
        world.get_component<Children>(restored_root).contains(restored_child)
    );
    CHECK(world.get_component<Children>(restored_child)
              .contains(restored_grandchild));
}

TEST_CASE(
    "Checkpoint store creates, lists, overwrites, and restores named retries",
    "[snapshot][checkpoint]"
) {
    register_game_types();
    auto world = make_game();
    snapshot::CheckpointStore checkpoints;
    checkpoints.resources().include<GameState>();

    const auto created = checkpoints.create("before-fight", world);
    REQUIRE(created);
    CHECK(created->revision == 1);
    CHECK(created->entity_count == 2);
    CHECK(created->resource_count == 1);

    attack(world);
    attack(world);
    REQUIRE(checkpoints.restore("before-fight", world));
    CHECK(summarize(world) == BranchSummary {0, 0, 0, 10, true, 7});

    const auto overwritten = checkpoints.create("before-fight", world);
    REQUIRE(overwritten);
    CHECK(overwritten->revision == 2);
    const auto listed = checkpoints.list();
    REQUIRE(listed.size() == 1);
    CHECK(listed.front().name == "before-fight");
    CHECK(listed.front().revision == 2);

    const auto missing = checkpoints.restore("missing", world);
    REQUIRE_FALSE(missing);
    CHECK(
        missing.error().kind ==
        snapshot::SnapshotError::Kind::CheckpointNotFound
    );
    const auto invalid = checkpoints.create("contains spaces", world);
    REQUIRE_FALSE(invalid);
    CHECK(
        invalid.error().kind ==
        snapshot::SnapshotError::Kind::InvalidCheckpointName
    );
}
