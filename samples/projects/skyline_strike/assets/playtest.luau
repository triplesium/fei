local core = require("@entisium/core")
local Gameplay = require("./gameplay")

local function begin_step(ctx, action)
    local control = ctx:resource(Gameplay.ControlState)
    control.agent_active = 1
    control.agent_x = action.move_x
    control.agent_y = action.move_y
    control.agent_fire = if action.fire then 1 else 0
end

local function end_step(ctx)
    local control = ctx:resource(Gameplay.ControlState)
    control.agent_x = 0.0
    control.agent_y = 0.0
    control.agent_fire = 0
    control.agent_active = 0
end

local function observe(ctx)
    local state = ctx:resource(Gameplay.GameState)
    local players = ctx:query {
        Read(Gameplay.Player),
        Read(core.Transform2d),
    }
    local enemies = ctx:query {
        Entity,
        Read(core.Transform2d),
        Read(Gameplay.Enemy),
        Read(Gameplay.Health),
    }
    local projectiles = ctx:query {
        Entity,
        Read(core.Transform2d),
        Read(Gameplay.Projectile),
    }

    local player_x = 0.0
    local player_y = 0.0
    local player_speed = 0.0
    local invincible_seconds = 0.0
    local fire_cooldown_seconds = 0.0
    for player, transform in players do
        player_x = transform.position.x
        player_y = transform.position.y
        player_speed = player.speed
        invincible_seconds = player.invincible
        fire_cooldown_seconds = player.fire_timer
    end

    local enemy_targets = {}
    for entity, transform, enemy, health in enemies do
        if enemy.active ~= 0 and health.current > 0.0 then
            enemy_targets[tostring(entity)] = {
                x = transform.position.x,
                y = transform.position.y,
                kind = if enemy.kind == 1 then "boss" else "fighter",
                health = health.current,
                max_health = health.maximum,
                radius = if enemy.kind == 1 then 1.35 else 0.47,
            }
        end
    end

    local hostile_projectiles = {}
    for entity, transform, projectile in projectiles do
        if projectile.team ~= 0 and projectile.damage > 0.0 then
            hostile_projectiles[tostring(entity)] = {
                x = transform.position.x,
                y = transform.position.y,
                velocity_x = projectile.velocity_x,
                velocity_y = projectile.velocity_y,
                radius = projectile.radius,
                damage = projectile.damage,
            }
        end
    end

    return {
        phase = state.phase,
        elapsed = state.elapsed,
        score = state.score,
        kills = state.kills,
        player_health = state.player_health,
        player = {
            x = player_x,
            y = player_y,
            speed = player_speed,
            radius = 0.34,
            invincible_seconds = invincible_seconds,
            fire_cooldown_seconds = fire_cooldown_seconds,
            bounds = {min_x = -8.0, max_x = 2.7, min_y = -4.25, max_y = 4.25},
        },
        boss_spawned = state.boss_spawned ~= 0,
        boss_health = state.boss_health,
        enemies = enemies:size(),
        projectiles = projectiles:size(),
        enemy_targets = enemy_targets,
        hostile_projectiles = hostile_projectiles,
        won = state.phase == 1,
        lost = state.phase == 2,
    }
end

export local SkylineStrikePlaytestPlugin = Plugin.new {
    build = function(app: App)
        app:add_playtest {
            id = "skyline-strike.main",
            label = "Skyline Strike",
            description = "Pilot the interceptor, survive enemy fire, and destroy the boss. World coordinates: +x right, +y up; positions/radii in world units, velocities in units/second, times in simulated seconds; fixed ticks are 1/60 second. Shots travel right; firing has no ammo cost. enemy_targets and hostile_projectiles contain all active objects, including offscreen objects, without truncation. Keys are entity IDs stable only for that entity's lifetime in this run; map order has no meaning. Use pairs() in Luau. Observing does not advance time.",
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
                        description = "Horizontal movement: -1 left, +1 right, 0 neutral. Combined movement vector is normalized when its length exceeds 1.",
                    },
                    move_y = {
                        type = "number",
                        minimum = -1.0,
                        maximum = 1.0,
                        description = "Vertical movement: -1 down, +1 up, 0 neutral. Movement is clamped to player.bounds.",
                    },
                    fire = {type = "boolean", description = "Hold fire for this action. Shoots right at 10.8 units/second from x+1, at the player's y. Cooldown is 0.145 seconds; damage is 1 per shot; no ammunition limit."},
                },
                required = {"move_x", "move_y", "fire"},
                additionalProperties = false,
            },
            observation = {
                type = "object",
                description = "Current simulation state. Spatial maps include all active targets/threats; scalar counts are retained for compatibility and projectiles counts both teams.",
                properties = {
                    phase = {type = "integer", description = "0 playing, 1 victory, 2 defeat."},
                    elapsed = {type = "number", description = "Elapsed gameplay seconds; stops advancing after victory/defeat."},
                    score = {type = "integer", description = "Score earned from destroyed enemies."},
                    kills = {type = "integer", description = "Destroyed enemies, including the boss."},
                    player_health = {type = "integer", description = "Remaining player hit points; starts at 8. Zero means defeat."},
                    player = {
                        type = "object",
                        properties = {
                            x = {type = "number", description = "Player center, world x (+right)."},
                            y = {type = "number", description = "Player center, world y (+up)."},
                            speed = {type = "number", description = "Maximum movement speed in world units/second."},
                            radius = {type = "number", description = "Circular hit radius against enemy projectiles. Collision occurs when center distance is at most the sum of radii; sprite size is not the hitbox."},
                            invincible_seconds = {type = "number", description = "Remaining immunity to enemy projectiles, in simulated seconds; zero means vulnerable."},
                            fire_cooldown_seconds = {type = "number", description = "Seconds until the next shot is ready; zero permits firing on the next gameplay tick."},
                            bounds = {
                                type = "object",
                                description = "Inclusive movement limits for the player center, in world coordinates. These are not enemy or projectile bounds.",
                                properties = {
                                    min_x = {type = "number", description = "Left movement limit."},
                                    max_x = {type = "number", description = "Right movement limit."},
                                    min_y = {type = "number", description = "Bottom movement limit."},
                                    max_y = {type = "number", description = "Top movement limit."},
                                },
                                required = {"min_x", "max_x", "min_y", "max_y"},
                                additionalProperties = false,
                            },
                        },
                        required = {"x", "y", "speed", "radius", "invincible_seconds", "fire_cooldown_seconds", "bounds"},
                        additionalProperties = false,
                    },
                    boss_spawned = {type = "boolean", description = "Whether the boss has spawned during this run; remains true after its defeat."},
                    boss_health = {type = "integer", description = "Rounded boss HP (initially 28 even before spawn). Use boss_spawned and enemy_targets to locate a living boss."},
                    enemies = {type = "integer", description = "Enemy entity count, including the boss. Use enemy_targets for active target details."},
                    projectiles = {type = "integer", description = "Projectile entity count for both teams. Use hostile_projectiles for damaging enemy shots only."},
                    enemy_targets = {
                        type = "object",
                        description = "All living active enemies keyed by entity ID string, including offscreen enemies and boss. Empty object when none; not truncated. Enemy bodies currently cause no contact damage.",
                        additionalProperties = {
                            type = "object",
                            properties = {
                                x = {type = "number", description = "Target center, world x."},
                                y = {type = "number", description = "Target center, world y."},
                                kind = {type = "string", enum = {"fighter", "boss"}, description = "Enemy type."},
                                health = {type = "number", description = "Current hit points."},
                                max_health = {type = "number", description = "Maximum hit points."},
                                radius = {type = "number", description = "Circular radius for collision with player shots, in world units."},
                            },
                            required = {"x", "y", "kind", "health", "max_health", "radius"},
                            additionalProperties = false,
                        },
                    },
                    hostile_projectiles = {
                        type = "object",
                        description = "All enemy projectiles with positive damage, keyed by entity ID string. Empty object when none; not truncated. Velocity is constant; predict a future center as position + velocity * seconds.",
                        additionalProperties = {
                            type = "object",
                            properties = {
                                x = {type = "number", description = "Projectile center, world x."},
                                y = {type = "number", description = "Projectile center, world y."},
                                velocity_x = {type = "number", description = "Horizontal world units/second; negative travels left."},
                                velocity_y = {type = "number", description = "Vertical world units/second; positive travels up."},
                                radius = {type = "number", description = "Circular collision radius in world units."},
                                damage = {type = "number", description = "Hit points removed on collision when the player is vulnerable."},
                            },
                            required = {"x", "y", "velocity_x", "velocity_y", "radius", "damage"},
                            additionalProperties = false,
                        },
                    },
                    won = {type = "boolean", description = "Boss destroyed and victory reached; stop issuing gameplay actions."},
                    lost = {type = "boolean", description = "Player defeated; stop issuing gameplay actions."},
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
                    "enemy_targets",
                    "hostile_projectiles",
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
