export type Player = {
    speed: f32,
}

export type Collectible = {
    value: i32,
}

export type GameState = {
    player: entity,
    collectible: entity,
    score: i32,
}

export type PlaytestInput = {
    move_x: f32,
    move_y: f32,
}

local function initialize(
    commands: Commands,
    assets: ResRW<AssetServer>,
    state: ResRW<GameState>
)
    if state.player ~= 0 then
        return
    end

    local image = assets:load(Image, "textures/awesomeface.png")
    assert(assets:is_loaded(image))

    commands:spawn(
        Camera2d.new {
            vertical_size = 6.0,
        },
        Transform2d.new()
    )

    local player = commands:spawn(
        Sprite.new {
            image = image,
            size = Vector2.new(1.2, 1.2),
        },
        Transform2d.new {
            position = Vector2.new(-2.5, 0.0),
        },
        Player.new {speed = 3.5}
    )

    local collectible = commands:spawn(
        Sprite.new {
            image = image,
            size = Vector2.new(0.7, 0.7),
            color = Color4F.new(1.0, 0.75, 0.2, 1.0),
        },
        Transform2d.new {
            position = Vector2.new(2.0, 0.0),
        },
        Collectible.new {value = 1}
    )

    state.player = player:id()
    state.collectible = collectible:id()
end

local function move_player(
    input: ResRO<KeyInput>,
    playtest_input: ResRO<PlaytestInput>,
    time: ResRO<Time>,
    players: Query<
        Write<Transform2d>,
        Read<Player>
    >
)
    local direction_x = playtest_input.move_x
    local direction_y = playtest_input.move_y

    if input:pressed(KeyCode.Left) or input:pressed(KeyCode.A) then
        direction_x -= 1.0
    end
    if input:pressed(KeyCode.Right) or input:pressed(KeyCode.D) then
        direction_x += 1.0
    end
    if input:pressed(KeyCode.Down) or input:pressed(KeyCode.S) then
        direction_y -= 1.0
    end
    if input:pressed(KeyCode.Up) or input:pressed(KeyCode.W) then
        direction_y += 1.0
    end

    local length = math.sqrt(
        direction_x * direction_x + direction_y * direction_y
    )
    if length > 0.0 then
        direction_x /= length
        direction_y /= length
    end

    for transform, player in players do
        local distance = player.speed * time:delta()
        transform.position.x += direction_x * distance
        transform.position.y += direction_y * distance
        transform.position.x = math.clamp(transform.position.x, -4.7, 4.7)
        transform.position.y = math.clamp(transform.position.y, -2.4, 2.4)
    end
end

local function collect(
    players: Query<Read<Transform2d>, With<Player>>,
    collectibles: Query<Write<Transform2d>, Read<Collectible>>,
    state: ResRW<GameState>
)
    for player_transform in players do
        for collectible_transform, collectible in collectibles do
            local dx = math.abs(
                player_transform.position.x - collectible_transform.position.x
            )
            local dy = math.abs(
                player_transform.position.y - collectible_transform.position.y
            )
            if dx <= 0.95 and dy <= 0.95 then
                state.score += collectible.value
                collectible_transform.position.x =
                    ((state.score * 1.7) % 8.0) - 4.0
                collectible_transform.position.y =
                    ((state.score * 2.3) % 4.4) - 2.2
                print("Score", state.score)
            end
        end
    end
end

export local MovementPlugin = Plugin.new {
    build = function(app: App)
        app:add_resource(GameState {})
        app:add_resource(PlaytestInput {})
        app:add_system(StartUp, initialize)
        app:add_systems(Update, move_player, collect)
    end,
}
