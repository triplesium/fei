#include "app/app.hpp"
#include "ecs/world.hpp"
#include "refl/cls.hpp"
#include "refl/registry.hpp"
#include "scripting_luau/plugin.hpp"
#include "scripting_luau/script_system_registry.hpp"
#include "snapshot/world_snapshot.hpp"
#include "snapshot_runtime_luau/adapters.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

using namespace fei;

namespace {

constexpr std::string_view c_type_prefix = "sample.snapshot_game.";

enum class ActionKind { Move, Attack, UsePotion, Wait };

struct Action {
    ActionKind kind {ActionKind::Wait};
    int dx {};
    int dy {};
};

struct GameTypes {
    TypeId grid_position;
    TypeId fighter;
    TypeId status_effects;
    TypeId inventory;
    TypeId enemy_brain;
    TypeId loot;
    TypeId action_queue;
    TypeId dungeon_map;
    TypeId dungeon_state;
    TypeId rng;
    TypeId telemetry;
    TypeId combat_log;
};

TypeId require_type(std::string_view local_name) {
    const auto qualified_name =
        std::string(c_type_prefix) + std::string(local_name);
    auto type = Registry::instance().try_get_type(qualified_name);
    if (!type) {
        throw std::runtime_error("missing Luau type " + qualified_name);
    }
    return type->id();
}

GameTypes game_types() {
    return GameTypes {
        .grid_position = require_type("GridPosition"),
        .fighter = require_type("Fighter"),
        .status_effects = require_type("StatusEffects"),
        .inventory = require_type("Inventory"),
        .enemy_brain = require_type("EnemyBrain"),
        .loot = require_type("Loot"),
        .action_queue = require_type("ActionQueue"),
        .dungeon_map = require_type("DungeonMap"),
        .dungeon_state = require_type("DungeonState"),
        .rng = require_type("DeterministicRng"),
        .telemetry = require_type("Telemetry"),
        .combat_log = require_type("CombatLog"),
    };
}

template<class T>
T property(Ref object, TypeId owner, std::string_view name) {
    auto value = Registry::instance()
                     .get_cls(owner)
                     .get_property(std::string(name))
                     .get(object);
    if (!value) {
        throw std::runtime_error(
            "could not read property " + std::string(name) + ": " +
            value.error().message
        );
    }
    return value->get_const<T>();
}

template<class T>
void set_property(Ref object, TypeId owner, std::string_view name, T value) {
    auto status = Registry::instance()
                      .get_cls(owner)
                      .get_property(std::string(name))
                      .set(object, Ref(value));
    if (!status) {
        throw std::runtime_error(
            "could not write property " + std::string(name) + ": " +
            status.error().message
        );
    }
}

std::vector<Entity>
entities_with(const World& world, TypeId first, TypeId second) {
    std::vector<Entity> entities;
    for (const auto& [_, archetype] : world.archetypes()) {
        if (archetype.has_component(first) && archetype.has_component(second)) {
            entities.insert(
                entities.end(),
                archetype.entities().begin(),
                archetype.entities().end()
            );
        }
    }
    std::ranges::sort(entities);
    return entities;
}

std::string load_game_script() {
    const auto path = (std::filesystem::path(FEI_ASSETS_PATH).parent_path() /
                       "samples" / "snapshot_game.luau")
                          .lexically_normal();
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error(
            "could not open Luau game script at " + path.string()
        );
    }
    return std::string(
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>()
    );
}

void load_game(App& app) {
    app.resource<LuauScriptSystemRegistry>().queue_source(
        LuauScriptSource {
            .name = "snapshot_game.luau",
            .content = load_game_script(),
        }
    );
    app.update();
    const auto& errors =
        app.resource<LuauScriptSystemRegistry>().queue_errors();
    if (!errors.empty()) {
        throw std::runtime_error(errors.front().error.message);
    }
}

void configure_checkpoints(
    World& world,
    snapshot::CheckpointStore& checkpoints
) {
    auto configured = snapshot_runtime_luau::configure_luau_adapters(
        world,
        checkpoints.registry()
    );
    if (!configured) {
        throw std::runtime_error(configured.error().message);
    }
    for (const auto type : world.resource_types()) {
        if (!checkpoints.registry().resource_policy(type)) {
            checkpoints.registry().set_resource_policy(
                type,
                snapshot::ResourcePolicy::Ignore
            );
        }
    }

    const auto coverage = snapshot::audit(world, checkpoints.registry());
    if (coverage.ready && coverage.complete) {
        return;
    }
    std::string message = "snapshot audit failed";
    for (const auto& component : coverage.components) {
        if (!component.serializable) {
            message +=
                "\ncomponent " + component.type_name + ": " + component.message;
        }
    }
    for (const auto& resource : coverage.resources) {
        if (!resource.serializable) {
            message +=
                "\nresource " + resource.type_name + ": " + resource.message;
        }
    }
    throw std::runtime_error(message);
}

void queue_action(World& world, const GameTypes& types, Action action) {
    const Ref queue = world.resource(types.action_queue);
    set_property(queue, types.action_queue, "has_action", true);
    set_property(
        queue,
        types.action_queue,
        "kind",
        static_cast<int>(action.kind)
    );
    set_property(queue, types.action_queue, "dx", action.dx);
    set_property(queue, types.action_queue, "dy", action.dy);
}

void run_branch(
    App& app,
    const GameTypes& types,
    const std::vector<Action>& actions
) {
    for (const auto action : actions) {
        queue_action(app.world(), types, action);
        app.update();
        const Ref state = app.world().resource(types.dungeon_state);
        if (property<bool>(state, types.dungeon_state, "defeat")) {
            break;
        }
    }
}

std::string_view actor_name(int kind) {
    switch (kind) {
        case 0:
            return "hero";
        case 1:
            return "goblin";
        case 2:
            return "venom";
        case 3:
            return "archer";
        default:
            return "unknown";
    }
}

char actor_glyph(int kind) {
    switch (kind) {
        case 0:
            return '@';
        case 1:
            return 'g';
        case 2:
            return 'v';
        case 3:
            return 'a';
        default:
            return '?';
    }
}

bool wall(int x, int y, int width, int height) {
    if (x == 0 || y == 0 || x == width - 1 || y == height - 1) {
        return true;
    }
    constexpr std::array walls {
        std::array {4, 2},
        std::array {4, 3},
        std::array {4, 4},
        std::array {7, 1},
        std::array {7, 2},
        std::array {7, 4},
        std::array {7, 5},
    };
    return std::ranges::any_of(walls, [x, y](const auto& position) {
        return position[0] == x && position[1] == y;
    });
}

std::string summarize(const World& world, const GameTypes& types) {
    const Ref state = world.resource(types.dungeon_state);
    const Ref telemetry = world.resource(types.telemetry);
    const Ref rng = world.resource(types.rng);
    const Ref log = world.resource(types.combat_log);
    const auto player = property<Entity>(state, types.dungeon_state, "player");

    std::ostringstream output;
    output << "turn=" << property<int>(state, types.dungeon_state, "turn")
           << ";score=" << property<int>(state, types.dungeon_state, "score")
           << ";remaining="
           << property<int>(state, types.dungeon_state, "enemies_remaining")
           << ";removed="
           << property<int>(state, types.dungeon_state, "removed_actors")
           << ";victory="
           << property<bool>(state, types.dungeon_state, "victory")
           << ";defeat=" << property<bool>(state, types.dungeon_state, "defeat")
           << ";rng=" << property<int>(rng, types.rng, "state") << ";telemetry="
           << property<int>(telemetry, types.telemetry, "changed_fighters")
           << ',' << property<int>(telemetry, types.telemetry, "added_loot")
           << ','
           << property<int>(telemetry, types.telemetry, "removed_fighters")
           << ";log_count=" << property<int>(log, types.combat_log, "count")
           << ";last_log="
           << property<std::string>(log, types.combat_log, "last");

    std::vector<std::string> actors;
    for (const auto entity :
         entities_with(world, types.grid_position, types.fighter)) {
        const Ref position = world.get_component(entity, types.grid_position);
        const Ref fighter = world.get_component(entity, types.fighter);
        const Ref status = world.get_component(entity, types.status_effects);
        const auto kind = property<int>(fighter, types.fighter, "kind");
        const auto team = property<int>(fighter, types.fighter, "team");
        std::ostringstream actor;
        actor << actor_name(kind) << '@'
              << property<int>(position, types.grid_position, "x") << ','
              << property<int>(position, types.grid_position, "y") << ':'
              << property<int>(fighter, types.fighter, "hp") << '/'
              << property<int>(fighter, types.fighter, "max_hp") << ":p"
              << property<int>(status, types.status_effects, "poison_turns")
              << ":s" << property<int>(status, types.status_effects, "shield");
        if (team == 0) {
            actor << ":i"
                  << property<int>(
                         world.get_component(entity, types.inventory),
                         types.inventory,
                         "potions"
                     );
        } else {
            const auto target = property<Entity>(
                world.get_component(entity, types.enemy_brain),
                types.enemy_brain,
                "target"
            );
            actor << ":target=" << (target == player ? "player" : "invalid");
        }
        actors.push_back(actor.str());
    }
    std::ranges::sort(actors);
    for (const auto& actor : actors) {
        output << ";actor=" << actor;
    }

    std::vector<std::string> loot;
    for (const auto entity :
         entities_with(world, types.grid_position, types.loot)) {
        const Ref position = world.get_component(entity, types.grid_position);
        loot.push_back(
            std::to_string(property<int>(position, types.grid_position, "x")) +
            "," +
            std::to_string(property<int>(position, types.grid_position, "y")) +
            ":" +
            std::to_string(
                property<int>(
                    world.get_component(entity, types.loot),
                    types.loot,
                    "healing"
                )
            )
        );
    }
    std::ranges::sort(loot);
    for (const auto& item : loot) {
        output << ";loot=" << item;
    }
    return output.str();
}

std::uint64_t digest(std::string_view text) {
    std::uint64_t result = 14695981039346656037ULL;
    for (const auto byte : text) {
        result ^= static_cast<unsigned char>(byte);
        result *= 1099511628211ULL;
    }
    return result;
}

void print_map(
    const World& world,
    const GameTypes& types,
    std::string_view label
) {
    const Ref map = world.resource(types.dungeon_map);
    const auto width = property<int>(map, types.dungeon_map, "width");
    const auto height = property<int>(map, types.dungeon_map, "height");
    std::vector<std::string> rows(
        static_cast<std::size_t>(height),
        std::string(static_cast<std::size_t>(width), '.')
    );
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            if (wall(x, y, width, height)) {
                rows[static_cast<std::size_t>(y)][static_cast<std::size_t>(x)] =
                    '#';
            }
        }
    }

    const Ref state = world.resource(types.dungeon_state);
    const auto exit_x = property<int>(state, types.dungeon_state, "exit_x");
    const auto exit_y = property<int>(state, types.dungeon_state, "exit_y");
    rows[static_cast<std::size_t>(exit_y)][static_cast<std::size_t>(exit_x)] =
        '>';
    for (const auto entity :
         entities_with(world, types.grid_position, types.loot)) {
        const Ref position = world.get_component(entity, types.grid_position);
        const auto x = property<int>(position, types.grid_position, "x");
        const auto y = property<int>(position, types.grid_position, "y");
        rows[static_cast<std::size_t>(y)][static_cast<std::size_t>(x)] = '!';
    }
    for (const auto entity :
         entities_with(world, types.grid_position, types.fighter)) {
        const Ref position = world.get_component(entity, types.grid_position);
        const Ref fighter = world.get_component(entity, types.fighter);
        const auto x = property<int>(position, types.grid_position, "x");
        const auto y = property<int>(position, types.grid_position, "y");
        rows[static_cast<std::size_t>(y)][static_cast<std::size_t>(x)] =
            actor_glyph(property<int>(fighter, types.fighter, "kind"));
    }

    const auto player = property<Entity>(state, types.dungeon_state, "player");
    const Ref player_fighter = world.get_component(player, types.fighter);
    std::cout << "\n"
              << label << " | turn "
              << property<int>(state, types.dungeon_state, "turn") << " | hp "
              << property<int>(player_fighter, types.fighter, "hp") << '/'
              << property<int>(player_fighter, types.fighter, "max_hp")
              << " | score "
              << property<int>(state, types.dungeon_state, "score")
              << " | enemies "
              << property<int>(state, types.dungeon_state, "enemies_remaining")
              << '\n';
    for (const auto& row : rows) {
        std::cout << row << '\n';
    }
}

int fail(const std::string& message) {
    std::cerr << "validation failed: " << message << '\n';
    return 1;
}

} // namespace

int main() {
    try {
        App app;
        app.add_plugin<LuauScriptingPlugin>();
        app.finish();
        load_game(app);
        const auto types = game_types();

        const Ref initial_state = app.world().resource(types.dungeon_state);
        if (!property<bool>(
                initial_state,
                types.dungeon_state,
                "initialized"
            )) {
            return fail("Luau game did not initialize");
        }

        snapshot::CheckpointStore checkpoints;
        configure_checkpoints(app.world(), checkpoints);
        print_map(app.world(), types, "checkpoint turn-0 (Luau)");
        const auto checkpoint = checkpoints.create("turn-0", app.world(), true);
        if (!checkpoint) {
            return fail("capture failed: " + checkpoint.error().message);
        }

        const std::vector<Action> branch_a {
            Action {.kind = ActionKind::Wait},
            Action {.kind = ActionKind::Attack, .dx = 1},
            Action {.kind = ActionKind::Attack, .dx = 1},
            Action {.kind = ActionKind::Move, .dx = 1},
            Action {.kind = ActionKind::Attack, .dy = 1},
            Action {.kind = ActionKind::Attack, .dy = 1},
            Action {.kind = ActionKind::UsePotion},
        };
        const std::vector<Action> branch_b {
            Action {.kind = ActionKind::Move, .dy = 1},
            Action {.kind = ActionKind::Wait},
            Action {.kind = ActionKind::Move, .dy = 1},
            Action {.kind = ActionKind::Attack, .dx = 1},
            Action {.kind = ActionKind::UsePotion},
            Action {.kind = ActionKind::Wait},
            Action {.kind = ActionKind::Move, .dx = 1},
        };

        run_branch(app, types, branch_a);
        const auto expected_summary = summarize(app.world(), types);
        const auto expected_digest = digest(expected_summary);
        const Ref expected_state = app.world().resource(types.dungeon_state);
        const Ref expected_telemetry = app.world().resource(types.telemetry);
        const auto player =
            property<Entity>(expected_state, types.dungeon_state, "player");
        const Ref player_status =
            app.world().get_component(player, types.status_effects);
        if (property<int>(
                expected_state,
                types.dungeon_state,
                "removed_actors"
            ) == 0 ||
            property<int>(expected_telemetry, types.telemetry, "added_loot") ==
                0 ||
            property<int>(
                expected_telemetry,
                types.telemetry,
                "removed_fighters"
            ) == 0) {
            return fail(
                "branch A did not exercise scripted death, loot, and removal"
            );
        }
        if (property<int>(
                player_status,
                types.status_effects,
                "poison_turns"
            ) == 0) {
            return fail("branch A did not preserve scripted status effects");
        }
        print_map(app.world(), types, "branch A (Luau)");

        for (int retry = 0; retry < 100; ++retry) {
            const auto restored = checkpoints.restore("turn-0", app.world());
            if (!restored) {
                return fail("restore failed: " + restored.error().message);
            }
            run_branch(app, types, branch_a);
            if (summarize(app.world(), types) != expected_summary) {
                return fail(
                    "script retry " + std::to_string(retry + 1) +
                    " diverged from the first branch"
                );
            }
            if (checkpoints.total_bytes() != checkpoint->byte_size) {
                return fail("checkpoint storage changed during retries");
            }
        }

        const auto restored = checkpoints.restore("turn-0", app.world());
        if (!restored) {
            return fail("final restore failed: " + restored.error().message);
        }
        run_branch(app, types, branch_b);
        const auto alternate_digest = digest(summarize(app.world(), types));
        if (alternate_digest == expected_digest) {
            return fail("different scripted decisions produced the same state");
        }
        print_map(app.world(), types, "branch B after script restore");

        std::cout << "\ncheckpoint: " << checkpoint->entity_count
                  << " entities, " << checkpoint->resource_count
                  << " resources, " << checkpoint->byte_size << " bytes\n"
                  << "branch A digest: 0x" << std::hex << expected_digest
                  << "\nbranch B digest: 0x" << alternate_digest << std::dec
                  << "\nLuau deterministic retry verified: 100/100\n";
        return 0;
    } catch (const std::exception& error) {
        return fail(error.what());
    }
}
