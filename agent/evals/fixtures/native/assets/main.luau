export type WebProjectSprite = {}

local function initialize(
    commands: Commands,
    assets: ResRW<AssetServer>
)
    local image = assets:load(Image, "checker.ppm")
    if not assets:is_loaded(image) then
        local load_error = assets:load_error(image)
        error(if load_error then load_error.message else "checker.ppm did not load")
    end

    commands:spawn(
        Camera2d.new {
            vertical_size = 5.0,
        },
        Transform2d.new()
    )
    commands:spawn(
        Sprite.new {
            image = image,
            size = Vector2.new(2.5, 2.5),
        },
        Transform2d.new(),
        WebProjectSprite.new()
    )
    print("web project script started")
end

local function animate(
    time: ResRO<Time>,
    sprites: Query<Write<Transform2d>, Read<WebProjectSprite>>
)
    for transform in sprites do
        transform.rotation += 45.0 * time:delta()
    end
end

local function begin_playtest_step(ctx, action)
    for transform in ctx:query {
        Write(Transform2d),
        Read(WebProjectSprite),
    } do
        transform.rotation = action.rotation
    end
end

local function observe_playtest(ctx)
    local sprites = ctx:query {
        Read(Transform2d),
        Read(WebProjectSprite),
    }
    local transform = sprites:first()
    return {
        rotation = transform.rotation,
    }
end

export local GamePlugin = Plugin.new {
    build = function(app: App)
        app:add_system(StartUp, initialize)
        app:add_system(Update, animate)
        app:add_playtest {
            id = "browser-demo.main",
            label = "Browser Demo Controls",
            description = "Set and inspect the checkerboard sprite rotation.",

            ticks = {
                default = 1,
                min = 1,
                max = 60,
                overridable = true,
            },

            action = {
                type = "object",
                properties = {
                    rotation = {type = "number"},
                },
                required = {"rotation"},
                additionalProperties = false,
            },

            observation = {
                type = "object",
                properties = {
                    rotation = {type = "number"},
                },
                required = {"rotation"},
                additionalProperties = false,
            },

            begin_step = begin_playtest_step,
            observe = observe_playtest,
        }
    end,
}
