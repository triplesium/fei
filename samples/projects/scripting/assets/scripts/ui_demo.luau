export type UiDemoButton = {}
export type UiDemoLabel = {}
export type UiDemoState = {
    button: entity,
    clicks: i32,
}

local function initialize(
    commands: Commands,
    assets: ResRW<AssetServer>,
    state: ResRW<UiDemoState>
)
    if state.button ~= 0 then
        return
    end

    local image = assets:load(Image, "textures/awesomeface.png")
    local font = assets:load(
        text.Font,
        "embedded://fonts/Cousine-Regular.ttf"
    )
    assert(assets:is_loaded(image))
    if not assets:is_loaded(font) then
        error(assets:load_error(font) or "font did not load")
    end

    commands:spawn(
        Camera2d.new {
            clear_color = Color4F.new(0.025, 0.035, 0.06, 1.0),
        },
        Transform2d.new()
    )

    local root = commands:spawn(
        ui.Node.new {
            padding = ui.Edges.all(ui.Length.px(32.0)),
            gap = ui.Length.px(20.0),
            align_items = ui.AlignItems.Center,
            justify_content = ui.JustifyContent.Center,
        },
        ui.BackgroundColor.new {
            color = Color4F.new(0.045, 0.065, 0.11, 1.0),
        }
    )

    local panel = commands:spawn(
        ui.Node.new {
            width = ui.Length.px(420.0),
            height = ui.Length.px(300.0),
            padding = ui.Edges.all(ui.Length.px(24.0)),
            gap = ui.Length.px(18.0),
            align_items = ui.AlignItems.Center,
            justify_content = ui.JustifyContent.Center,
        },
        ui.BackgroundColor.new {
            color = Color4F.new(0.09, 0.13, 0.22, 1.0),
        },
        ui.BorderRadius.all(ui.Length.px(18.0))
    )
    panel:set_parent(root:id())

    local button = commands:spawn(
        ui.Node.new {
            width = ui.Length.px(240.0),
            height = ui.Length.px(150.0),
            padding = ui.Edges.all(ui.Length.px(18.0)),
            align_items = ui.AlignItems.Center,
            justify_content = ui.JustifyContent.Center,
        },
        ui.BackgroundColor.new {
            color = Color4F.new(0.16, 0.38, 0.72, 1.0),
        },
        ui.BorderRadius.all(ui.Length.px(14.0)),
        ui.Interaction.None,
        ui_widgets.Button.new(),
        UiDemoButton.new()
    )
    button:set_parent(panel:id())

    commands:spawn(
        ui.Node.new {
            width = ui.Length.px(96.0),
            height = ui.Length.px(96.0),
        },
        ui.ImageNode.new {
            image = image,
            mode = ui.NodeImageMode.Stretch,
        },
        ui.FocusPolicy.Pass
    ):set_parent(button:id())

    commands:spawn(
        ui.Node.new {
            height = ui.Length.px(24.0),
        },
        text.Text.new {
            value = "Click the face",
        },
        text.TextColor.new {
            color = Color4F.new(0.9, 0.94, 1.0, 1.0),
        },
        text.TextFont.new {
            font = font,
            font_size = 20.0,
        },
        UiDemoLabel.new()
    ):set_parent(panel:id())

    state.button = button:id()
end

local function update_button(
    mouse: ResRO<MouseInput>,
    state: ResRW<UiDemoState>,
    buttons: Query<
        Entity,
        Read<ui.Interaction>,
        Write<ui.BackgroundColor>,
        With<UiDemoButton>
    >,
    labels: Query<Write<text.Text>, With<UiDemoLabel>>
)
    for entity, interaction, background in buttons do
        local over = interaction ~= ui.Interaction.None
        background.color = if over
            then Color4F.new(0.22, 0.52, 0.9, 1.0)
            else Color4F.new(0.16, 0.38, 0.72, 1.0)

        if entity == state.button and over and
            mouse:just_released(MouseButton.Left) then
            state.clicks += 1
        end
    end

    for label in labels do
        local value = "Clicks: " .. tostring(state.clicks)
        if label.value ~= value then
            label.value = value
        end
    end
end

export local UiDemoPlugin = Plugin.new {
    build = function(app: App)
        app:add_resource(UiDemoState {})
        app:add_system(StartUp, initialize)
        app:add_system(Update, update_button)
    end,
}
