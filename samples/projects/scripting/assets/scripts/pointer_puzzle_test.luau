local function initialize(
    commands: Commands,
    assets: ResRW<AssetServer>,
    state: ResRW<PuzzleState>
)
    if state.initialized ~= 0 then
        return
    end

    local image = assets:load(Image, "textures/awesomeface.png")
    assert(assets:is_loaded(image))

    commands:spawn(
        Camera2d.new {
            vertical_size = 6.0,
            clear_color = Color4F.new(0.07, 0.08, 0.11, 1.0),
        },
        Transform2d.new()
    )

    commands:spawn(
        Sprite.new {
            image = image,
            size = Vector2.new(0.95, 0.95),
            color = Color4F.new(0.95, 0.22, 0.25, 0.3),
        },
        Transform2d.new {position = Vector2.new(2.2, -1.2)},
        PuzzleTarget.new {id = 1}
    )
    commands:spawn(
        Sprite.new {
            image = image,
            size = Vector2.new(0.95, 0.95),
            color = Color4F.new(0.25, 0.9, 0.4, 0.3),
        },
        Transform2d.new {position = Vector2.new(2.2, 1.2)},
        PuzzleTarget.new {id = 2}
    )
    commands:spawn(
        Sprite.new {
            image = image,
            size = Vector2.new(0.95, 0.95),
            color = Color4F.new(0.25, 0.55, 1.0, 0.3),
        },
        Transform2d.new {position = Vector2.new(2.2, 0.0)},
        PuzzleTarget.new {id = 3}
    )

    commands:spawn(
        Sprite.new {
            image = image,
            size = Vector2.new(0.72, 0.72),
            color = Color4F.new(0.95, 0.22, 0.25, 1.0),
            layer = 1,
        },
        Transform2d.new {position = Vector2.new(-2.5, 1.2)},
        PuzzlePiece.new {id = 1}
    )
    commands:spawn(
        Sprite.new {
            image = image,
            size = Vector2.new(0.72, 0.72),
            color = Color4F.new(0.25, 0.9, 0.4, 1.0),
            layer = 1,
        },
        Transform2d.new {position = Vector2.new(-2.5, 0.0)},
        PuzzlePiece.new {id = 2}
    )
    commands:spawn(
        Sprite.new {
            image = image,
            size = Vector2.new(0.72, 0.72),
            color = Color4F.new(0.25, 0.55, 1.0, 1.0),
            layer = 1,
        },
        Transform2d.new {position = Vector2.new(-2.5, -1.2)},
        PuzzlePiece.new {id = 3}
    )

    state.initialized = 1
end

return {
    types = {
        PuzzlePiece = {
            id = field(i32, 0),
            placed = field(i32, 0),
        },
        PuzzleTarget = {
            id = field(i32, 0),
        },
        PuzzleState = {
            initialized = field(i32, 0),
            selected = field(i32, 0),
            placed = field(i32, 0),
            actions = field(i32, 0),
        },
    },
    resources = {
        PuzzleState = {},
    },
    systems = {
        system(StartUp, initialize),
    },
}
