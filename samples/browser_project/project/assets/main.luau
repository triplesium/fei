local function initialize(
    commands: Commands,
    assets: ResRW<AssetServer>
)
    local image = assets:load(Image, "checker.ppm")
    if not assets:is_loaded(image) then
        error(assets:load_error(image) or "checker.ppm did not load")
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

return {
    types = {
        WebProjectSprite = {},
    },
    systems = {
        system(StartUp, initialize),
        system(Update, animate),
    },
}
