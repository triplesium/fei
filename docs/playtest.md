# Playtest

This guide covers the current structured playtest contract for browser projects and native C++ registration. It assumes the Editor runtime described in [Browser and Web Editor](browser.md) is already available.

Playtest exposes a game-specific, machine-readable control contract to agents. Instead of inferring every action from pixels and synthesizing keyboard input, an agent can discover structured actions, execute an exact number of fixed ticks, and read a structured observation of the resulting game state.

The contract is available through the Web Editor command registry, used by the built-in Pi agent and the local `entisium-editor` MCP server. Both clients use the same runtime inspection providers and Playtest registry.

Viewport capture and raw keyboard or pointer input remain available as a fallback for projects that do not declare a structured interface.

## Runtime model

A playtest interface contains an action JSON Schema, an observation JSON Schema, fixed-tick bounds, and three lifecycle callbacks:

1. `begin_step` receives a decoded, schema-validated action and writes it into game-owned control state.
2. The runtime advances the requested number of fixed 60 Hz ticks.
3. `end_step` clears temporary control state.
4. `observe` returns the new structured state, which is validated against the observation schema.
5. The playtest clock pauses again until another step is queued.

When at least one interface is registered, `PlaytestPlugin` preserves the game's previous clock settings, selects the fixed timestep, and pauses the simulation after startup. `play_observe` and viewport capture do not advance the simulation. Only one step may be active at a time, and its completed result must be consumed before another step can be queued.

Put gameplay that must advance during an agent step in `FixedPreUpdate`, `FixedUpdate`, or another fixed schedule. Ordinary `Update` systems do not represent deterministic game ticks and should normally be limited to presentation work while a structured playtest is active.

## Declaring an interface in a Luau Plugin

Declare playtests directly inside `Plugin.build` with `app:add_playtest`. A project does not need a separate playtest file, but keeping the declaration in a dedicated Plugin usually makes the game/control boundary clearer.

```luau
local core = require("@entisium/core")

export type AgentControl = {
    move_x: f32,
    move_y: f32,
}

local function apply_agent_control(
    time: ResRO<core.Time>,
    control: ResRO<AgentControl>,
    players: Query<Write<core.Transform2d>, With<Player>>
)
    for transform in players do
        transform.position.x += control.move_x * 3.0 * time:delta()
        transform.position.y += control.move_y * 3.0 * time:delta()
    end
end

local function begin_step(ctx, action)
    local control = ctx:resource(AgentControl)
    control.move_x = action.x
    control.move_y = action.y
end

local function end_step(ctx)
    local control = ctx:resource(AgentControl)
    control.move_x = 0.0
    control.move_y = 0.0
end

local function observe(ctx)
    local players = ctx:query {
        Read(core.Transform2d),
        With(Player),
    }
    local transform = players:first()
    return {
        player = {
            x = transform.position.x,
            y = transform.position.y,
        },
        won = ctx:resource(GameState).won ~= 0,
    }
end

export local AgentPlaytestPlugin = Plugin.new {
    dependencies = {GamePlugin},
    build = function(app: App)
        app:insert_resource(AgentControl {
            move_x = 0.0,
            move_y = 0.0,
        })
        app:add_system(FixedPreUpdate, apply_agent_control)
        app:add_playtest {
            id = "game.main",
            label = "Main game",
            description = "Move the player until the level is complete.",
            ticks = {
                default = 6,
                min = 1,
                max = 120,
                overridable = true,
            },
            action = {
                type = "object",
                properties = {
                    x = {type = "number", minimum = -1.0, maximum = 1.0},
                    y = {type = "number", minimum = -1.0, maximum = 1.0},
                },
                required = {"x", "y"},
                additionalProperties = false,
            },
            observation = {
                type = "object",
                properties = {
                    player = {
                        type = "object",
                        properties = {
                            x = {type = "number"},
                            y = {type = "number"},
                        },
                        required = {"x", "y"},
                        additionalProperties = false,
                    },
                    won = {type = "boolean"},
                },
                required = {"player", "won"},
                additionalProperties = false,
            },
            begin_step = begin_step,
            end_step = end_step,
            observe = observe,
        }
    end,
}
```

Add this Plugin to the project's exported Plugin group. Runtime Host installs `project_runtime::LuauPlaytestsPlugin` for Luau-scripted projects and registers all `app:add_playtest` declarations before the registry is frozen.

### Declaration fields

| Field | Required | Meaning |
| --- | --- | --- |
| `id` | yes | Stable, unique interface identifier such as `game.main`. |
| `label` | no | Human-readable name; defaults to `id`. |
| `description` | no | Goal and control summary presented to the agent. |
| `ticks.default` | no | Ticks used when the caller omits an override; defaults to 1. |
| `ticks.min` | no | Minimum accepted tick count; defaults to `default`. |
| `ticks.max` | no | Maximum accepted tick count; defaults to `default`. |
| `ticks.overridable` | no | Whether callers may supply `ticks`; defaults to `false`. |
| `action` | yes | JSON Schema used before `begin_step`. |
| `observation` | no | JSON Schema used after `observe`; defaults to an empty object. |
| `begin_step` | yes | Function `(ctx, action)` that applies the action. |
| `end_step` | no | Function `(ctx)` that releases transient action state. |
| `observe` | no | Function `(ctx)` returning JSON-compatible state; defaults to `{}`. |

Callbacks receive a borrowed dynamic World context. They may access reflected resources and queries from the Plugin module, but must not retain the context, query values, or borrowed references after the callback returns.

Actions and observations must contain JSON-compatible values. Null values are not supported. Conversion is bounded to a maximum nesting depth of 32 and 16,384 JSON nodes.

## Schema profile

Schemas use Entisium's bounded Draft 2020-12 profile. It supports:

- `type`, `enum`, `const`, local `$ref` and `$defs`;
- `allOf`, `anyOf`, `oneOf`, `not`, and `if`/`then`/`else`;
- object properties, required fields, and additional-property constraints;
- array items, prefix items, length constraints, and uniqueness;
- numeric bounds and multiples; and
- string length constraints.

Annotation keywords such as `title`, `description`, `default`, and `format` are accepted but do not change validation. Unsupported keywords or malformed schemas reject interface registration. Action failures and observation failures include an instance path such as `$.player.position[0]` where possible.

## Driving a game through Editor MCP

The Editor Host exposes a local Streamable HTTP MCP endpoint at `http://127.0.0.1:3100/mcp` by default:

```json
{
    "mcpServers": {
        "entisium-editor": {
            "url": "http://127.0.0.1:3100/mcp"
        }
    }
}
```

Keep the Editor page open because the host relays MCP calls to the currently connected page. A complete agent session follows this order:

1. Call `runtime_play` and wait until the runtime is running.
2. Call `play_interfaces` before choosing an action.
3. Select an interface and call `play_observe` for its initial state.
4. Call `play_step` with an action matching `action_schema` and, when allowed, a tick override.
5. Poll `play_step_status` with the returned `request_id` until it reports `completed` or `failed`. A terminal status consumes the completion.
6. Repeat observation and stepping until the observation reports the project's completion condition.
7. Optionally call `runtime_observe` to capture the final rendered frame.
8. Always call `runtime_stop`, including after errors or aborted attempts.

Conceptually, clients should use `try/finally` around the session:

```text
runtime_play()
try
    discovered = play_interfaces()
    interface = discovered.interfaces[0]
    state = play_observe(interface.id)
    while not complete(state)
        queued = play_step(interface.id, choose_action(state))
        state = poll_until_terminal(queued.request_id).observation
finally
    runtime_clear_input()
    runtime_stop()
```

`play_step` is asynchronous because execution happens on the runtime's game thread. Do not queue another step while the previous completion is unread. Prefer the structured observation over screenshots for decisions, and use screenshots to verify presentation or when no structured interface exists.

The structured controller and normal keyboard controller can both affect the same game state. Call `runtime_clear_input` before structured play if an agent may have left a key or pointer button held, and design gameplay so structured control does not accidentally combine with ordinary input. `end_step` should always return action-owned resources to a neutral state.

## Native C++ registration

Native games install `runtime_protocol::PlaytestPlugin` and register a `PlaytestInterfaceRegistration` during Plugin setup. The descriptor contains the same IDs, tick bounds, and schemas as a Luau declaration. The callbacks receive `World&`; `begin_step` and `observe` exchange JSON strings.

```cpp
void GamePlaytestPlugin::dependencies(PluginDependencies& dependencies) const {
    dependencies.require<runtime_protocol::PlaytestPlugin>();
    dependencies.require<GamePlugin>();
}

void GamePlaytestPlugin::setup(App& app) {
    auto registered = runtime_protocol::register_playtest_interface(
        app,
        runtime_protocol::PlaytestInterfaceRegistration {
            .descriptor = runtime_protocol::PlaytestInterfaceDescriptor {
                .id = "game.main",
                .label = "Main game",
                .description = "Move until the level is complete.",
                .decision_ticks = 6,
                .minimum_ticks = 1,
                .maximum_ticks = 120,
                .allow_tick_override = true,
                .action_schema_json = R"({"type":"object"})",
                .observation_schema_json = R"({"type":"object"})",
            },
            .begin_step = begin_game_step,
            .end_step = end_game_step,
            .observe = observe_game,
        }
    );
    if (!registered) {
        throw std::runtime_error(registered.error().message);
    }
}
```

Registration must finish before `PlaytestRegistry` is frozen. Prefer the Plugin dependency above instead of manually adding registry resources.

## Design guidance

- Expose semantic actions at the game's decision boundary: movement vectors, card choices, target positions, or menu commands.
- Include the actual completion signal (`won`, `complete`, or equivalent) in every observation so an agent knows when to stop.
- Include enough state to choose the next action, but avoid renderer-only or unstable implementation details.
- Use stable IDs and strict schemas with `required` and `additionalProperties = false` where practical.
- Keep tick ranges bounded. Large steps reduce opportunities to react and make failures harder to diagnose.
- Apply actions through game-owned resources, consume them from fixed systems, and clear them in `end_step`.
- Keep `observe` free of mutations, random sampling, rendering, and time advancement.
- Make restart/reset behavior deterministic and never leave held inputs behind.
- Stop the runtime when the agent reaches a terminal condition or abandons the attempt.

## Troubleshooting

| Symptom | Check |
| --- | --- |
| `play_interfaces` is unavailable | The runtime must be running, the Editor page must remain connected, and the project must install a playtest interface. |
| An interface is missing | Ensure its Plugin is exported by the project and its `app:add_playtest` call runs during Plugin build. |
| Registration fails | Check for duplicate IDs, invalid tick bounds, malformed schemas, unsupported schema keywords, or missing callbacks. |
| A step is rejected as busy | Poll and consume the previous request with `play_step_status` before queuing another. |
| The game moves between steps | Put simulation state changes in fixed schedules and ensure the runtime includes `PlaytestPlugin`. |
| Movement differs from the action | Release held input with `runtime_clear_input` and avoid combining the normal input controller with the playtest controller. |
| A completed game keeps running | Treat `runtime_stop` as required session cleanup, not an optional action. |

## Examples and implementation references

- [`samples/browser_project/project/assets/main.luau`](../samples/browser_project/project/assets/main.luau) contains a minimal Luau declaration.
- [`samples/projects/scripting/README.md`](../samples/projects/scripting/README.md) lists platformer, pointer, card battle, and checkpoint examples.
- [`runtime_protocol/playtest.hpp`](../engine/runtime_protocol/include/runtime_protocol/playtest.hpp) defines the native interface descriptor and registry.
- [`runtime_protocol/playtest_runner.hpp`](../engine/runtime_protocol/include/runtime_protocol/playtest_runner.hpp) defines queued step execution and completion state.
