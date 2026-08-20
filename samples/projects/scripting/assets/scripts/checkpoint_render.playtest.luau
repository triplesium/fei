local function player(ctx, state)
    local value = ctx:entity(state.player)
    assert(value ~= nil, "checkpoint arena player is missing")
    return value
end

local function enemy(ctx, state)
    if state.enemy_alive == 0 then
        return nil
    end
    local value = ctx:entity(state.enemy)
    assert(value ~= nil, "checkpoint arena enemy is missing")
    return value
end

local function begin_step(ctx, action)
    local state = ctx:resource(CheckpointArenaState)
    if state.move_axis ~= 0.0 or
        state.move_action_started ~= 0 or
        state.jump_requested ~= 0 or
        state.attack_requested ~= 0 then
        error("the checkpoint arena already has queued controls")
    end

    if action.kind == "advance" then
        state.move_axis = 1.0
        state.move_action_started = 1
    elseif action.kind == "retreat" then
        state.move_axis = -1.0
        state.move_action_started = 1
    elseif action.kind == "jump" then
        state.jump_requested = 1
    elseif action.kind == "strike" then
        if enemy(ctx, state) == nil then
            error("the checkpoint arena enemy is already defeated")
        end
        state.attack_requested = action.power
    else
        error("unsupported checkpoint arena action '" .. tostring(action.kind) .. "'")
    end
end

local function end_step(ctx)
    local state = ctx:resource(CheckpointArenaState)
    state.move_axis = 0.0
    state.move_action_started = 0
    state.jump_requested = 0
    state.attack_requested = 0
end

local function observe(ctx)
    local state = ctx:resource(CheckpointArenaState)
    local player_entity = player(ctx, state)
    local player_transform = player_entity:get(Transform2d)
    local player_actor = player_entity:get(CheckpointArenaActor)
    local player_controller = player_entity:get(CheckpointArenaPlayer)

    local enemy_health = 0
    local enemy_x = 0.0
    local enemy_entity = enemy(ctx, state)
    if enemy_entity ~= nil then
        enemy_health = enemy_entity:get(CheckpointArenaActor).health
        enemy_x = enemy_entity:get(Transform2d).position.x
    end

    return {
        turn = state.turn,
        actions = state.actions,
        score = state.score,
        player = {
            x = player_transform.position.x,
            y = player_transform.position.y,
            velocity_y = player_controller.velocity_y,
            grounded = player_controller.grounded ~= 0,
            health = player_actor.health,
        },
        enemy = {
            alive = state.enemy_alive ~= 0,
            x = enemy_x,
            health = enemy_health,
        },
    }
end

return playtest {
    id = "checkpoint-arena.main",
    label = "Rendered Checkpoint Arena",
    description = "Uses the same queued gameplay actions as physical keyboard " ..
        "input, updates the rendered HUD, and supports deterministic " ..
        "checkpoint branches.",

    types = {
        CheckpointArenaActor = "sample.checkpoint_render.CheckpointArenaActor",
        CheckpointArenaPlayer = "sample.checkpoint_render.CheckpointArenaPlayer",
        CheckpointArenaState = "sample.checkpoint_render.CheckpointArenaState",
    },

    ticks = {
        default = 1,
        min = 1,
        max = 120,
        overridable = true,
    },

    action = {
        type = "object",
        properties = {
            kind = {
                type = "string",
                enum = {"advance", "retreat", "jump", "strike"},
            },
            power = {
                type = "integer",
                enum = {0, 3},
            },
        },
        required = {"kind", "power"},
        additionalProperties = false,
    },

    observation = {
        type = "object",
        properties = {
            turn = {type = "integer"},
            actions = {type = "integer"},
            score = {type = "integer"},
            player = {
                type = "object",
                properties = {
                    x = {type = "number"},
                    y = {type = "number"},
                    velocity_y = {type = "number"},
                    grounded = {type = "boolean"},
                    health = {type = "integer"},
                },
                required = {
                    "x",
                    "y",
                    "velocity_y",
                    "grounded",
                    "health",
                },
                additionalProperties = false,
            },
            enemy = {type = "object"},
        },
        required = {"turn", "actions", "score", "player", "enemy"},
        additionalProperties = false,
    },

    begin_step = begin_step,
    end_step = end_step,
    observe = observe,
}
