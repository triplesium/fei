local floor_y = -1.85

local function initialize(
    commands: Commands,
    assets: ResRW<AssetServer>,
    state: ResRW<PlatformState>
)
    if state.player ~= 0 then
        return
    end

    local image = assets:load(Image, "textures/awesomeface.png")
    assert(assets:is_loaded(image))

    commands:spawn(
        Camera2d.new {
            vertical_size = 6.0,
            clear_color = Color4F.new(0.04, 0.06, 0.12, 1.0),
        },
        Transform2d.new()
    )

    commands:spawn(
        Sprite.new {
            image = image,
            size = Vector2.new(10.0, 0.45),
            color = Color4F.new(0.22, 0.28, 0.38, 1.0),
        },
        Transform2d.new {
            position = Vector2.new(0.0, -2.25),
        }
    )

    commands:spawn(
        Sprite.new {
            image = image,
            size = Vector2.new(0.55, 1.1),
            color = Color4F.new(0.9, 0.2, 0.25, 1.0),
            layer = 1,
        },
        Transform2d.new {
            position = Vector2.new(0.0, -1.45),
        }
    )

    commands:spawn(
        Sprite.new {
            image = image,
            size = Vector2.new(0.8, 0.8),
            color = Color4F.new(1.0, 0.8, 0.15, 1.0),
            layer = 1,
        },
        Transform2d.new {
            position = Vector2.new(3.0, floor_y),
        }
    )

    local player = commands:spawn(
        Sprite.new {
            image = image,
            size = Vector2.new(0.72, 0.72),
            color = Color4F.new(0.25, 0.65, 1.0, 1.0),
            layer = 2,
        },
        Transform2d.new {
            position = Vector2.new(-3.0, floor_y),
        },
        PlatformPlayer.new()
    )
    state.player = player:id()
end

local function simulate(
    input: ResRW<PlatformInput>,
    state: ResRW<PlatformState>,
    time: ResRO<Time>,
    players: Query<Write<Transform2d>, Write<PlatformPlayer>>
)
    local delta = time:delta()
    for transform, player in players do
        if input.jump ~= 0 and player.grounded ~= 0 then
            player.velocity_y = 5.0
            player.grounded = 0
            input.jump = 0
        end

        local previous_x = transform.position.x
        transform.position.x += input.horizontal * player.speed * delta
        player.velocity_y -= 12.0 * delta
        transform.position.y += player.velocity_y * delta

        if transform.position.y <= floor_y then
            transform.position.y = floor_y
            player.velocity_y = 0.0
            player.grounded = 1
        end

        if transform.position.x > -0.45 and
            transform.position.x < 0.45 and
            transform.position.y < -1.0 then
            if previous_x <= -0.45 then
                transform.position.x = -0.45
            elseif previous_x >= 0.45 then
                transform.position.x = 0.45
            end
        end

        transform.position.x = math.clamp(transform.position.x, -4.8, 4.8)
        if transform.position.x >= 2.55 then
            state.won = 1
        end
    end
    state.ticks += 1
end

return {
    types = {
        PlatformPlayer = {
            velocity_y = field(f32, 0.0),
            speed = field(f32, 5.0),
            grounded = field(i32, 1),
        },
        PlatformInput = {
            horizontal = field(f32, 0.0),
            jump = field(i32, 0),
        },
        PlatformState = {
            player = field(entity, 0),
            won = field(i32, 0),
            ticks = field(i32, 0),
        },
    },
    resources = {
        PlatformInput = {},
        PlatformState = {},
    },
    systems = {
        system(StartUp, initialize),
        system(Update, simulate),
    },
}
