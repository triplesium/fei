local function initialize(
    world: World,
    assets: ResRW<AssetServer>,
    state: ResRW<ScriptingSampleState>
)
    if state.mover ~= 0 then
        return
    end

    local image = assets:load(Image, "textures/awesomeface.png")
    assert(assets:is_loaded(image))

    world:spawn(
        Camera2d.new {
            vertical_size = 6.0,
        },
        Transform2d.new()
    )

    local mover = world:spawn(
        Sprite.new {
            image = image,
            size = Vector2.new(1.8, 1.8),
        },
        Transform2d.new(),
        ScriptingSamplePosition.new(),
        ScriptingSampleVelocity.new()
    )
    state.mover = mover:id()
end

local function move(
    movers: Query<
        Write<Transform2d>,
        Read<ScriptingSampleVelocity>,
        With<ScriptingSamplePosition>
    >,
    state: ResRW<ScriptingSampleState>
)
    for transform, velocity in movers do
        transform.position.x += velocity.x
        if transform.position.x > 3.8 then
            transform.position.x = -3.8
        end
    end
    state.ticks += 1
end

return module {
    name = "sample.movement",
    types = {
        ScriptingSamplePosition = {
            x = field(f32, 1.0),
        },
        ScriptingSampleVelocity = {
            x = field(f32, 0.01),
        },
        ScriptingSampleState = {
            mover = field(entity, 0),
            ticks = field(i32, 0),
        },
    },
    resources = {
        ScriptingSampleState = {},
    },
    systems = {
        system(StartUp, initialize),
        system(Update, move),
    },
}
