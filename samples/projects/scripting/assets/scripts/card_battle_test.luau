export type CardBattleState = {
    initialized: i32,
    turn: i32,
    player_hp: i32,
    enemy_hp: i32,
    energy: i32,
    actions: i32,
    status: i32,
}

local function initialize(
    commands: Commands,
    assets: ResRW<AssetServer>,
    state: ResRW<CardBattleState>
)
    if state.initialized ~= 0 then
        return
    end

    local image = assets:load(Image, "textures/awesomeface.png")
    assert(assets:is_loaded(image))

    commands:spawn(
        Camera2d.new {
            vertical_size = 6.0,
            clear_color = Color4F.new(0.035, 0.09, 0.075, 1.0),
        },
        Transform2d.new()
    )

    commands:spawn(
        Sprite.new {
            image = image,
            size = Vector2.new(1.15, 1.15),
            color = Color4F.new(0.95, 0.28, 0.25, 1.0),
        },
        Transform2d.new {position = Vector2.new(0.0, 1.45)}
    )
    commands:spawn(
        Sprite.new {
            image = image,
            size = Vector2.new(0.9, 0.9),
            color = Color4F.new(0.25, 0.65, 1.0, 1.0),
        },
        Transform2d.new {position = Vector2.new(-3.4, 1.45)}
    )

    commands:spawn(
        Sprite.new {
            image = image,
            size = Vector2.new(1.35, 1.8),
            color = Color4F.new(0.95, 0.45, 0.2, 1.0),
            layer = 1,
        },
        Transform2d.new {position = Vector2.new(-2.2, -1.25)}
    )
    commands:spawn(
        Sprite.new {
            image = image,
            size = Vector2.new(1.35, 1.8),
            color = Color4F.new(0.7, 0.25, 0.95, 1.0),
            layer = 1,
        },
        Transform2d.new {position = Vector2.new(0.0, -1.25)}
    )
    commands:spawn(
        Sprite.new {
            image = image,
            size = Vector2.new(1.35, 1.8),
            color = Color4F.new(0.25, 0.9, 0.45, 1.0),
            layer = 1,
        },
        Transform2d.new {position = Vector2.new(2.2, -1.25)}
    )

    state.initialized = 1
    state.turn = 1
    state.player_hp = 10
    state.enemy_hp = 6
    state.energy = 3
end

export local CardBattlePlugin = Plugin.new {
    build = function(app: App)
        app:add_resource(CardBattleState {})
        app:add_system(StartUp, initialize)
    end,
}
