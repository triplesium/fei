local function initialize(world: World, state: ResRW<ScriptingSampleState>)
    if state.mover ~= 0 then
        return
    end

    local mover = world:spawn(
        ScriptingSamplePosition.new(),
        ScriptingSampleVelocity.new()
    )
    state.mover = mover:id()
end

local function move(
    movers: Query<
        Write<ScriptingSamplePosition>,
        Read<ScriptingSampleVelocity>
    >,
    state: ResRW<ScriptingSampleState>
)
    for position, velocity in movers do
        position.x += velocity.x
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
            x = field(f32, 2.0),
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
