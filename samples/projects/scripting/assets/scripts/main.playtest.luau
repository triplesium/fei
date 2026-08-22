local function begin_step(ctx, action)
    local input = ctx:resource(PlaytestInput)
    input.move_x = 0.0
    input.move_y = 0.0

    if action.move == "left" then
        input.move_x = -1.0
    elseif action.move == "right" then
        input.move_x = 1.0
    elseif action.move == "down" then
        input.move_y = -1.0
    elseif action.move == "up" then
        input.move_y = 1.0
    elseif action.move ~= "none" then
        error("unsupported move '" .. tostring(action.move) .. "'")
    end
end

local function end_step(ctx)
    local input = ctx:resource(PlaytestInput)
    input.move_x = 0.0
    input.move_y = 0.0
end

local function observe(ctx)
    local players = ctx:query {
        Read(Transform2d),
        With(Player),
    }
    local transform = players:first()
    local state = ctx:resource(GameState)

    return {
        player = {
            x = transform.position.x,
            y = transform.position.y,
        },
        score = state.score,
    }
end

return playtest {
    id = "game.main",
    label = "Main Game Controls",
    description = "Moves the player and reports the current game state.",

    types = {
        PlaytestInput = "project.scripts.movement.PlaytestInput",
        GameState = "project.scripts.movement.GameState",
        Player = "project.scripts.movement.Player",
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
            move = {
                type = "string",
                enum = {"none", "left", "right", "down", "up"},
            },
        },
        required = {"move"},
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
            },
            score = {type = "integer"},
        },
        required = {"player", "score"},
    },

    begin_step = begin_step,
    end_step = end_step,
    observe = observe,
}
