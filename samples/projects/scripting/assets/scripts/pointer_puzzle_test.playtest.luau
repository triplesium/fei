local function near(position, point)
    local dx = position.x - point.x
    local dy = position.y - point.y
    return dx * dx + dy * dy <= 0.36
end

local function click(ctx, action, state)
    state.selected = 0
    local pieces = ctx:query {
        Read(Transform2d),
        Read(PuzzlePiece),
    }
    for transform, piece in pieces do
        if piece.placed == 0 and near(transform.position, action.from) then
            state.selected = piece.id
            return
        end
    end
end

local function drag(ctx, action, state)
    local pieces = ctx:query {
        Write(Transform2d),
        Write(PuzzlePiece),
    }
    local targets = ctx:query {
        Read(Transform2d),
        Read(PuzzleTarget),
    }
    for piece_transform, piece in pieces do
        if piece.id == state.selected and
            piece.placed == 0 and
            near(piece_transform.position, action.from) then
            for target_transform, target in targets do
                if target.id == piece.id and
                    near(target_transform.position, action.to) then
                    piece_transform.position.x = target_transform.position.x
                    piece_transform.position.y = target_transform.position.y
                    piece.placed = 1
                    state.placed += 1
                    state.selected = 0
                    return
                end
            end
        end
    end
    state.selected = 0
end

local function begin_step(ctx, action)
    local state = ctx:resource(PuzzleState)
    state.actions += 1
    if action.kind == "click" then
        click(ctx, action, state)
    elseif action.kind == "drag" then
        drag(ctx, action, state)
    else
        error("unsupported pointer action '" .. tostring(action.kind) .. "'")
    end
end

local function observe(ctx)
    local state = ctx:resource(PuzzleState)
    local pieces = {}
    for transform, piece in ctx:query {
        Read(Transform2d),
        Read(PuzzlePiece),
    } do
        table.insert(pieces, {
            id = piece.id,
            x = transform.position.x,
            y = transform.position.y,
            placed = piece.placed ~= 0,
        })
    end

    local targets = {}
    for transform, target in ctx:query {
        Read(Transform2d),
        Read(PuzzleTarget),
    } do
        table.insert(targets, {
            id = target.id,
            x = transform.position.x,
            y = transform.position.y,
        })
    end

    return {
        pieces = pieces,
        targets = targets,
        selected = state.selected,
        placed = state.placed,
        actions = state.actions,
        complete = state.placed == 3,
    }
end

local point_schema = {
    type = "object",
    properties = {
        x = {type = "number"},
        y = {type = "number"},
    },
    required = {"x", "y"},
    additionalProperties = false,
}

return playtest {
    id = "pointer-puzzle.main",
    label = "Pointer Puzzle",
    description = "Uses world-space pointer coordinates. Click a piece to " ..
        "select it, then drag it onto the target with the same id.",

    types = {
        PuzzlePiece = "sample.pointer_puzzle_test.PuzzlePiece",
        PuzzleTarget = "sample.pointer_puzzle_test.PuzzleTarget",
        PuzzleState = "sample.pointer_puzzle_test.PuzzleState",
    },

    ticks = {
        default = 1,
        min = 1,
        max = 1,
        overridable = false,
    },

    action = {
        type = "object",
        properties = {
            kind = {
                type = "string",
                enum = {"click", "drag"},
            },
            from = point_schema,
            to = point_schema,
        },
        required = {"kind", "from", "to"},
        additionalProperties = false,
    },

    observation = {
        type = "object",
        properties = {
            pieces = {type = "array"},
            targets = {type = "array"},
            selected = {type = "integer"},
            placed = {type = "integer"},
            actions = {type = "integer"},
            complete = {type = "boolean"},
        },
        required = {
            "pieces",
            "targets",
            "selected",
            "placed",
            "actions",
            "complete",
        },
        additionalProperties = false,
    },

    begin_step = begin_step,
    observe = observe,
}
