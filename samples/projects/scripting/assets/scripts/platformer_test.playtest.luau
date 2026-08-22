local function begin_step(ctx, action)
    local input = ctx:resource(PlatformInput)
    input.horizontal = action.horizontal
    input.jump = if action.jump then 1 else 0
end

local function end_step(ctx)
    local input = ctx:resource(PlatformInput)
    input.horizontal = 0.0
    input.jump = 0
end

local function observe(ctx)
    local state = ctx:resource(PlatformState)
    local players = ctx:query {
        Read(Transform2d),
        Read(PlatformPlayer),
    }
    local transform, player = players:first()
    return {
        player = {
            x = transform.position.x,
            y = transform.position.y,
            velocity_y = player.velocity_y,
            grounded = player.grounded ~= 0,
        },
        ticks = state.ticks,
        won = state.won ~= 0,
    }
end

return playtest {
    id = "platformer.main",
    label = "Platformer Movement",
    description = "Combines an analog horizontal axis with a jump impulse. " ..
        "The red barrier can only be crossed while airborne.",

    types = {
        PlatformInput = "project.scripts.platformer_test.PlatformInput",
        PlatformPlayer = "project.scripts.platformer_test.PlatformPlayer",
        PlatformState = "project.scripts.platformer_test.PlatformState",
    },

    ticks = {
        default = 6,
        min = 1,
        max = 120,
        overridable = true,
    },

    action = {
        type = "object",
        properties = {
            horizontal = {
                type = "number",
                minimum = -1.0,
                maximum = 1.0,
            },
            jump = {type = "boolean"},
        },
        required = {"horizontal", "jump"},
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
                    velocity_y = {type = "number"},
                    grounded = {type = "boolean"},
                },
                required = {"x", "y", "velocity_y", "grounded"},
                additionalProperties = false,
            },
            ticks = {type = "integer"},
            won = {type = "boolean"},
        },
        required = {"player", "ticks", "won"},
        additionalProperties = false,
    },

    begin_step = begin_step,
    end_step = end_step,
    observe = observe,
}
