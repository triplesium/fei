#include "app/app.hpp"
#include "ecs/world.hpp"
#include "project/project.hpp"
#include "refl/cls.hpp"
#include "refl/registry.hpp"
#include "runtime_inspection/provider.hpp"
#include "runtime_inspection/registry.hpp"
#include "runtime_inspection_snapshot/checkpoint.hpp"
#include "runtime_protocol/probe.hpp"
#include "snapshot/archive.hpp"
#include "snapshot/world_snapshot.hpp"

#include <chrono>
#include <cstdint>
#include <exception>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

namespace {

using Json = nlohmann::json;
using namespace ets;

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

struct DeterministicState {
    std::uint64_t rng_state {0x9e3779b97f4a7c15ULL};
    std::uint64_t simulation_tick {};
    int last_roll {};
};

void register_game_types() {
    auto& registry = Registry::instance();
    registry.register_cls<Position>({"snapshot_runtime_fixture"}, "Position")
        .add_property("x", &Position::x);
    registry.register_cls<Health>({"snapshot_runtime_fixture"}, "Health")
        .add_property("value", &Health::value);
    registry.register_cls<Hunter>({"snapshot_runtime_fixture"}, "Hunter")
        .add_property("target", &Hunter::target);
    registry.register_cls<GameState>({"snapshot_runtime_fixture"}, "GameState")
        .add_property("turn", &GameState::turn)
        .add_property("score", &GameState::score)
        .add_property("player", &GameState::player)
        .add_property("enemy", &GameState::enemy);
    registry
        .register_cls<DeterministicState>(
            {"snapshot_runtime_fixture"},
            "DeterministicState"
        )
        .add_property("rng_state", &DeterministicState::rng_state)
        .add_property("simulation_tick", &DeterministicState::simulation_tick)
        .add_property("last_roll", &DeterministicState::last_roll);
}

int next_roll(DeterministicState& state) {
    state.rng_state ^= state.rng_state >> 12U;
    state.rng_state ^= state.rng_state << 25U;
    state.rng_state ^= state.rng_state >> 27U;
    return static_cast<int>((state.rng_state * 0x2545f4914f6cdd1dULL) % 3U);
}

void setup_game(World& world) {
    const auto player = world.entity();
    world.add_component(player, Position {.x = 0});
    world.add_component(player, Health {.value = 10});

    const auto enemy = world.entity();
    world.add_component(enemy, Position {.x = 2});
    world.add_component(enemy, Health {.value = 7});
    world.add_component(enemy, Hunter {.target = player});
    world.add_resource(
        GameState {
            .player = player,
            .enemy = enemy,
        }
    );
    world.add_resource(DeterministicState {});
}

Result<std::string, runtime_inspection::InspectionError>
validate_empty_request(std::string_view payload) {
    try {
        const auto request = Json::parse(payload);
        if (!request.is_object() || !request.empty()) {
            return failure(
                runtime_inspection::InspectionError {
                    .kind =
                        runtime_inspection::InspectionErrorKind::InvalidRequest,
                    .message = "Game fixture request must be an empty object",
                }
            );
        }
    } catch (const std::exception& error) {
        return failure(
            runtime_inspection::InspectionError {
                .kind = runtime_inspection::InspectionErrorKind::InvalidRequest,
                .message = error.what(),
            }
        );
    }
    return std::string {};
}

std::string observe_game(const World& world) {
    const auto& state = world.resource<GameState>();
    const auto& deterministic = world.resource<DeterministicState>();
    const auto enemy_alive = world.has_entity(state.enemy);
    return Json {
        {"turn", state.turn},
        {"score", state.score},
        {"simulation_tick", deterministic.simulation_tick},
        {"last_roll", deterministic.last_roll},
        {"player",
         {
             {"x", world.get_component<Position>(state.player).x},
             {"health", world.get_component<Health>(state.player).value},
         }},
        {"enemy",
         {
             {"alive", enemy_alive},
             {"health",
              enemy_alive ? world.get_component<Health>(state.enemy).value : 0},
         }},
    }
        .dump();
}

Result<std::string, runtime_inspection::InspectionError>
apply_game_action(World& world, std::string_view action) {
    auto& state = world.resource<GameState>();
    auto& deterministic = world.resource<DeterministicState>();
    if (action != "attack" && action != "wait") {
        return failure(
            runtime_inspection::InspectionError {
                .kind = runtime_inspection::InspectionErrorKind::InvalidRequest,
                .message = "Game action must be 'attack' or 'wait'",
            }
        );
    }
    if (action == "attack" && !world.has_entity(state.enemy)) {
        return failure(
            runtime_inspection::InspectionError {
                .kind = runtime_inspection::InspectionErrorKind::Conflict,
                .message = "Enemy is already defeated",
            }
        );
    }
    ++state.turn;
    ++deterministic.simulation_tick;
    if (action == "wait") {
        deterministic.last_roll = 0;
        return observe_game(world);
    }
    deterministic.last_roll = next_roll(deterministic);
    world.get_component_rw<Health>(state.player)->value -= 2;
    auto enemy_health = world.get_component_rw<Health>(state.enemy);
    enemy_health->value -= 3 + deterministic.last_roll;
    if (enemy_health->value <= 0) {
        world.despawn(state.enemy);
        state.score += 10;
    }
    return observe_game(world);
}

runtime_inspection::InspectionDescriptor
game_descriptor(std::string id, std::string label, bool read_only) {
    return runtime_inspection::InspectionDescriptor {
        .id = std::move(id),
        .label = std::move(label),
        .description = "Controls the deterministic snapshot runtime fixture.",
        .schema = read_only ? "test.game.observe.v1" : "test.game.attack.v1",
        .read_only = read_only,
        .cost = runtime_inspection::InspectionCost::Low,
        .request_schema_json =
            R"({"type":"object","additionalProperties":false})",
        .response_schema_json = R"({"type":"object"})",
    };
}

Status<runtime_inspection::InspectionError>
register_game_providers(runtime_inspection::InspectionRegistry& registry) {
    auto status = registry.add(
        game_descriptor("test.game.observe", "Observe Test Game", true),
        [](World& world, std::string_view payload)
            -> Result<std::string, runtime_inspection::InspectionError> {
            auto valid = validate_empty_request(payload);
            if (!valid) {
                return failure(std::move(valid.error()));
            }
            return observe_game(world);
        }
    );
    if (!status) {
        return status;
    }
    return registry.add(
        game_descriptor("test.game.attack", "Attack in Test Game", false),
        [](World& world, std::string_view payload)
            -> Result<std::string, runtime_inspection::InspectionError> {
            auto valid = validate_empty_request(payload);
            if (!valid) {
                return failure(std::move(valid.error()));
            }
            return apply_game_action(world, "attack");
        }
    );
}

runtime_inspection::InspectionDescriptor play_descriptor(
    std::string id,
    std::string label,
    std::string schema,
    bool read_only,
    std::string request_schema
) {
    return runtime_inspection::InspectionDescriptor {
        .id = std::move(id),
        .label = std::move(label),
        .description = "Standard play control for the snapshot fixture.",
        .schema = std::move(schema),
        .read_only = read_only,
        .cost = runtime_inspection::InspectionCost::Low,
        .request_schema_json = std::move(request_schema),
        .response_schema_json = R"({"type":"object"})",
    };
}

Status<runtime_inspection::InspectionError>
register_play_providers(runtime_inspection::InspectionRegistry& registry) {
    auto status = registry.add(
        play_descriptor(
            "play.interfaces",
            "List Play Interfaces",
            "play.interfaces.v1",
            true,
            R"({"type":"object","additionalProperties":false})"
        ),
        [](World&, std::string_view payload)
            -> Result<std::string, runtime_inspection::InspectionError> {
            auto valid = validate_empty_request(payload);
            if (!valid) {
                return failure(std::move(valid.error()));
            }
            return Json {
                {"interfaces",
                 Json::array({
                     {
                         {"id", "game.combat"},
                         {"label", "Snapshot Combat"},
                         {"description",
                          "Deterministic attack or wait decisions."},
                         {"decision_ticks", 1},
                         {"minimum_ticks", 1},
                         {"maximum_ticks", 1},
                         {"allow_tick_override", false},
                         {"action_schema",
                          {
                              {"type", "object"},
                              {"additionalProperties", false},
                              {"required", Json::array({"kind"})},
                              {"properties",
                               {{"kind",
                                 {{"enum", Json::array({"attack", "wait"})}}}}},
                          }},
                         {"observation_schema", {{"type", "object"}}},
                     },
                 })},
            }
                .dump();
        }
    );
    if (!status) {
        return status;
    }
    status = registry.add(
        play_descriptor(
            "play.observe",
            "Observe Play Interface",
            "play.observe.v1",
            true,
            R"({"type":"object","additionalProperties":false,"required":["interface"],"properties":{"interface":{"type":"string"}}})"
        ),
        [](World& world, std::string_view payload)
            -> Result<std::string, runtime_inspection::InspectionError> {
            try {
                const auto request = Json::parse(payload);
                if (!request.is_object() || request.size() != 1 ||
                    request.value("interface", "") != "game.combat") {
                    return failure(
                        runtime_inspection::InspectionError {
                            .kind = runtime_inspection::InspectionErrorKind::
                                InvalidRequest,
                            .message =
                                "play.observe requires interface game.combat",
                        }
                    );
                }
                const auto& deterministic =
                    world.resource<DeterministicState>();
                return Json {
                    {"interface", "game.combat"},
                    {"frame", deterministic.simulation_tick},
                    {"observation", Json::parse(observe_game(world))},
                }
                    .dump();
            } catch (const std::exception& error) {
                return failure(
                    runtime_inspection::InspectionError {
                        .kind = runtime_inspection::InspectionErrorKind::
                            InvalidRequest,
                        .message = error.what(),
                    }
                );
            }
        }
    );
    if (!status) {
        return status;
    }
    return registry.add(
        play_descriptor(
            "play.step",
            "Step Play Interface",
            "play.step.v1",
            false,
            R"({"type":"object","additionalProperties":false,"required":["interface","action"],"properties":{"interface":{"type":"string"},"action":{"type":"object"},"ticks":{"type":"integer","minimum":1,"maximum":1}}})"
        ),
        [](World& world, std::string_view payload)
            -> Result<std::string, runtime_inspection::InspectionError> {
            try {
                const auto request = Json::parse(payload);
                if (!request.is_object() ||
                    (request.size() != 2 && request.size() != 3) ||
                    request.value("interface", "") != "game.combat" ||
                    !request.contains("action") ||
                    !request.at("action").is_object() ||
                    request.at("action").size() != 1 ||
                    !request.at("action").contains("kind") ||
                    !request.at("action").at("kind").is_string() ||
                    (request.contains("ticks") && request.at("ticks") != 1)) {
                    return failure(
                        runtime_inspection::InspectionError {
                            .kind = runtime_inspection::InspectionErrorKind::
                                InvalidRequest,
                            .message = "Invalid game.combat play.step request",
                        }
                    );
                }
                auto observation = apply_game_action(
                    world,
                    request.at("action").at("kind").get<std::string_view>()
                );
                if (!observation) {
                    return failure(std::move(observation.error()));
                }
                const auto& deterministic =
                    world.resource<DeterministicState>();
                return Json {
                    {"interface", "game.combat"},
                    {"ticks", 1},
                    {"frame", deterministic.simulation_tick},
                    {"stopped", false},
                    {"observation", Json::parse(*observation)},
                }
                    .dump();
            } catch (const std::exception& error) {
                return failure(
                    runtime_inspection::InspectionError {
                        .kind = runtime_inspection::InspectionErrorKind::
                            InvalidRequest,
                        .message = error.what(),
                    }
                );
            }
        }
    );
}

Result<std::string, runtime_protocol::RuntimeInspectionError> inspect_runtime(
    World& world,
    const runtime_protocol::InspectionRequest& request
) {
    auto response =
        world.resource<runtime_inspection::InspectionRegistry>().dispatch(
            world,
            runtime_inspection::InspectionInvocation {
                .provider = request.provider,
                .schema = request.schema,
                .payload_json = request.payload_json,
            }
        );
    if (!response) {
        return failure(
            runtime_protocol::RuntimeInspectionError {
                .kind = std::string(
                    runtime_inspection::inspection_error_kind_name(
                        response.error().kind
                    )
                ),
                .message = std::move(response.error().message),
            }
        );
    }
    return std::move(*response);
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        return 1;
    }
    auto project = ets::Project::load(argv[1]);
    if (!project) {
        return 1;
    }

    register_game_types();
    ets::App app;
    setup_game(app.world());
    ets::snapshot::CheckpointStore checkpoints;
    checkpoints.registry().resources().include<GameState>();
    checkpoints.registry().resources().include<DeterministicState>();
    checkpoints.registry().resource<ets::AppStates>(
        ets::snapshot::ResourcePolicy::Ignore
    );
    checkpoints.registry().resource<ets::CommandsQueue>(
        ets::snapshot::ResourcePolicy::Ignore
    );
    checkpoints.registry()
        .resource<ets::runtime_inspection::InspectionRegistry>(
            ets::snapshot::ResourcePolicy::Ignore
        );
    checkpoints.registry().resource<ets::runtime_protocol::RuntimeProbe>(
        ets::snapshot::ResourcePolicy::Ignore
    );
    checkpoints.registry().resource<ets::snapshot::CheckpointStore>(
        ets::snapshot::ResourcePolicy::Ignore
    );
    checkpoints.registry().resource<ets::snapshot::SnapshotArchiveMetadata>(
        ets::snapshot::ResourcePolicy::Ignore
    );

    ets::runtime_inspection::InspectionRegistry inspections;
    auto registered = ets::runtime_inspection::checkpoint::
        register_checkpoint_inspection_providers(inspections);
    if (!registered) {
        return 1;
    }
    registered = register_game_providers(inspections);
    if (!registered) {
        return 1;
    }
    registered = register_play_providers(inspections);
    if (!registered) {
        return 1;
    }
    inspections.freeze();

    ets::runtime_protocol::RuntimeProbeConfig probe {
        .project = project->config().name,
        .project_file = project->project_file().generic_string(),
        .build_id = "snapshot-runtime-fixture-v4",
        .heartbeat_interval_ms = 100,
        .inspection_handler = inspect_runtime,
    };
    for (const auto& descriptor : inspections.descriptors()) {
        probe.inspections.push_back(
            ets::runtime_protocol::InspectionCapability {
                .id = descriptor.id,
                .label = descriptor.label,
                .description = descriptor.description,
                .schema = descriptor.schema,
                .read_only = descriptor.read_only,
                .cost = std::string(
                    ets::runtime_inspection::inspection_cost_name(
                        descriptor.cost
                    )
                ),
                .request_schema_json = descriptor.request_schema_json,
                .response_schema_json = descriptor.response_schema_json,
            }
        );
    }

    app.add_resource(std::move(checkpoints));
    app.add_resource(
        ets::snapshot::SnapshotArchiveMetadata {
            .project = project->config().name,
            .engine_build = "snapshot-runtime-fixture-v4",
            .runtime_signature = "checkpoint-runtime-fixture-v1",
            .script_hash = "no-scripts",
        }
    );
    app.add_resource(std::move(inspections));
    app.add_plugin(
        ets::runtime_protocol::RuntimeProbePlugin {std::move(probe)}
    );
    app.startup();
    for (int frame = 0; frame < 6000; ++frame) {
        app.update();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    app.shutdown();
    return 0;
}
