local core = require("@entisium/core")
local Gameplay = require("./gameplay")

local function begin_step(ctx, action)
    local control = ctx:resource(Gameplay.ControlState)
    control.agent_x = action.move_x
    control.agent_y = action.move_y
    control.agent_fire = if action.fire then 1 else 0
end

local function end_step(ctx)
    local control = ctx:resource(Gameplay.ControlState)
    control.agent_x = 0.0
    control.agent_y = 0.0
    control.agent_fire = 0
end

local function observe(ctx)
    local state = ctx:resource(Gameplay.GameState)
    local players = ctx:query {
        Read(core.Transform2d),
        With(Gameplay.Player),
    }
    local transform = players:first()
    local enemies = ctx:query {
        Read(Gameplay.Enemy),
    }
    local projectiles = ctx:query {
        Read(Gameplay.Projectile),
    }

    return {
        phase = state.phase,
        elapsed = state.elapsed,
        score = state.score,
        kills = state.kills,
        player_health = state.player_health,
        player = {
            x = transform.position.x,
            y = transform.position.y,
        },
        boss_spawned = state.boss_spawned ~= 0,
        boss_health = state.boss_health,
        enemies = enemies:size(),
        projectiles = projectiles:size(),
        won = state.phase == 1,
        lost = state.phase == 2,
    }
end

export local PlaytestPlugin = Plugin.new {
    dependencies = {Gameplay.GameplayPlugin},
    build = function(app: App)
        app:add_playtest {
            id = "skyline-strike.main",
            label = "Skyline Strike",
            description = "Pilot the interceptor, fire continuously, survive the wave, and destroy the dreadnought.",
            ticks = {
                default = 6,
                min = 1,
                max = 120,
                overridable = true,
            },
            action = {
                type = "object",
                properties = {
                    move_x = {
                        type = "number",
                        minimum = -1.0,
                        maximum = 1.0,
                    },
                    move_y = {
                        type = "number",
                        minimum = -1.0,
                        maximum = 1.0,
                    },
                    fire = {type = "boolean"},
                },
                required = {"move_x", "move_y", "fire"},
                additionalProperties = false,
            },
            observation = {
                type = "object",
                properties = {
                    phase = {type = "integer"},
                    elapsed = {type = "number"},
                    score = {type = "integer"},
                    kills = {type = "integer"},
                    player_health = {type = "integer"},
                    player = {
                        type = "object",
                        properties = {
                            x = {type = "number"},
                            y = {type = "number"},
                        },
                        required = {"x", "y"},
                        additionalProperties = false,
                    },
                    boss_spawned = {type = "boolean"},
                    boss_health = {type = "integer"},
                    enemies = {type = "integer"},
                    projectiles = {type = "integer"},
                    won = {type = "boolean"},
                    lost = {type = "boolean"},
                },
                required = {
                    "phase",
                    "elapsed",
                    "score",
                    "kills",
                    "player_health",
                    "player",
                    "boss_spawned",
                    "boss_health",
                    "enemies",
                    "projectiles",
                    "won",
                    "lost",
                },
                additionalProperties = false,
            },
            begin_step = begin_step,
            end_step = end_step,
            observe = observe,
        }
    end,
}
