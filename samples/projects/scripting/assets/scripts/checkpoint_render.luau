export type CheckpointArenaActor = {
    kind: i32,
    health: i32,
}

export type CheckpointArenaPlayer = {
    velocity_y: f32,
    speed: f32,
    grounded: i32,
}

export type CheckpointArenaState = {
    initialized: i32,
    player: entity,
    enemy: entity?,
    physics_probe: entity,
    enemy_alive: i32,
    turn: i32,
    actions: i32,
    score: i32,
    move_axis: f32,
    move_action_started: i32,
    jump_requested: i32,
    attack_requested: i32,
}

export type ArenaUiRoot = {}
export type ArenaStatusLabel = {}
export type ArenaEnemyHealthFill = {}
export type ArenaVictoryPanel = {}
export type CheckpointArenaPhysicsProbe = {}

local function initialize(
    commands: Commands,
    assets: ResRW<AssetServer>,
    state: ResRW<CheckpointArenaState>
)
    if state.initialized ~= 0 then
        return
    end

    local background = assets:load(
        Image,
        "textures/checkpoint_arena/forest_ruins.png"
    )
    local player_image = assets:load(
        Image,
        "textures/checkpoint_arena/adventurer.png"
    )
    local enemy_image = assets:load(
        Image,
        "textures/checkpoint_arena/ember_slime.png"
    )
    local font = assets:load(
        text.Font,
        "embedded://fonts/Cousine-Regular.ttf"
    )
    assert(assets:is_loaded(background))
    assert(assets:is_loaded(player_image))
    assert(assets:is_loaded(enemy_image))
    if not assets:is_loaded(font) then
        error(assets:load_error(font) or "font did not load")
    end

    commands:spawn(
        Camera2d.new {
            vertical_size = 6.0,
            clear_color = Color4F.new(0.018, 0.028, 0.075, 1.0),
        },
        Transform2d.new()
    )

    commands:spawn(
        Sprite.new {
            image = background,
            size = Vector2.new(10.67, 6.0),
            layer = -20,
        },
        Transform2d.new()
    )

    local player = commands:spawn(
        Sprite.new {
            image = player_image,
            size = Vector2.new(1.15, 1.35),
            layer = 2,
        },
        Transform2d.new {position = Vector2.new(-3.35, -1.15)},
        CheckpointArenaActor.new {kind = 1, health = 10},
        CheckpointArenaPlayer.new {speed = 3.5, grounded = 1}
    )
    local enemy = commands:spawn(
        Sprite.new {
            image = enemy_image,
            size = Vector2.new(1.55, 1.05),
            layer = 2,
        },
        Transform2d.new {position = Vector2.new(2.65, -1.15)},
        CheckpointArenaActor.new {kind = 2, health = 6}
    )

    local physics_probe = commands:spawn(
        Sprite.new {
            image = enemy_image,
            size = Vector2.new(0.38, 0.38),
            color = Color4F.new(0.45, 0.9, 1.0, 1.0),
            layer = 3,
        },
        Transform2d.new {position = Vector2.new(-1.25, 1.65)},
        RigidBody2d.new {
            gravity_scale = 0.15,
            fixed_rotation = true,
        },
        Collider2d.circle(0.19),
        LinearVelocity2d.new {value = Vector2.new(0.65, 0.0)},
        CheckpointArenaPhysicsProbe.new()
    )

    local ui_root = commands:spawn(
        ui.Node.new {
            width = ui.Length.percent(100.0),
            height = ui.Length.percent(100.0),
            align_items = ui.AlignItems.Center,
            justify_content = ui.JustifyContent.Center,
        },
        ArenaUiRoot.new()
    )

    local status_panel = commands:spawn(
        ui.Node.new {
            position_type = ui.PositionType.Absolute,
            width = ui.Length.px(620.0),
            height = ui.Length.px(92.0),
            left = ui.Length.px(24.0),
            top = ui.Length.px(24.0),
            padding = ui.Edges.all(ui.Length.px(14.0)),
            gap = ui.Length.px(8.0),
        },
        ui.BackgroundColor.new {
            color = Color4F.new(0.025, 0.045, 0.095, 0.9),
        },
        ui.BorderRadius.all(ui.Length.px(12.0))
    )
    status_panel:set_parent(ui_root:id())

    commands:spawn(
        ui.Node.new {height = ui.Length.px(28.0)},
        text.Text.new {
            value = "HP 10   EMBER GUARDIAN 6   SCORE 0   ACTIONS 0",
        },
        text.TextColor.new {
            color = Color4F.new(0.9, 0.95, 1.0, 1.0),
        },
        text.TextFont.new {font = font, font_size = 18.0},
        ArenaStatusLabel.new()
    ):set_parent(status_panel:id())

    local health_track = commands:spawn(
        ui.Node.new {
            width = ui.Length.px(590.0),
            height = ui.Length.px(12.0),
        },
        ui.BackgroundColor.new {
            color = Color4F.new(0.16, 0.105, 0.18, 1.0),
        },
        ui.BorderRadius.all(ui.Length.px(6.0))
    )
    health_track:set_parent(status_panel:id())

    commands:spawn(
        ui.Node.new {
            width = ui.Length.percent(100.0),
            height = ui.Length.percent(100.0),
        },
        ui.BackgroundColor.new {
            color = Color4F.new(1.0, 0.38, 0.1, 1.0),
        },
        ui.BorderRadius.all(ui.Length.px(6.0)),
        ArenaEnemyHealthFill.new()
    ):set_parent(health_track:id())

    local objective = commands:spawn(
        ui.Node.new {
            position_type = ui.PositionType.Absolute,
            width = ui.Length.px(385.0),
            height = ui.Length.px(72.0),
            right = ui.Length.px(24.0),
            top = ui.Length.px(24.0),
            padding = ui.Edges.all(ui.Length.px(14.0)),
            align_items = ui.AlignItems.Center,
            justify_content = ui.JustifyContent.Center,
        },
        ui.BackgroundColor.new {
            color = Color4F.new(0.025, 0.045, 0.095, 0.82),
        },
        ui.BorderRadius.all(ui.Length.px(12.0))
    )
    objective:set_parent(ui_root:id())

    commands:spawn(
        text.Text.new {
            value = "FOREST GATE  •  DEFEAT THE EMBER GUARDIAN",
        },
        text.TextColor.new {
            color = Color4F.new(0.73, 0.9, 1.0, 1.0),
        },
        text.TextFont.new {font = font, font_size = 16.0}
    ):set_parent(objective:id())

    local controls = commands:spawn(
        ui.Node.new {
            position_type = ui.PositionType.Absolute,
            width = ui.Length.px(900.0),
            height = ui.Length.px(50.0),
            left = ui.Length.px(24.0),
            bottom = ui.Length.px(24.0),
            padding = ui.Edges.all(ui.Length.px(12.0)),
            align_items = ui.AlignItems.Center,
            justify_content = ui.JustifyContent.Center,
        },
        ui.BackgroundColor.new {
            color = Color4F.new(0.025, 0.045, 0.095, 0.86),
        },
        ui.BorderRadius.all(ui.Length.px(12.0))
    )
    controls:set_parent(ui_root:id())

    commands:spawn(
        text.Text.new {
            value = "A / D  MOVE   W / ↑  JUMP   SPACE  ATTACK   PGUP  SAVE   PGDN  REWIND",
        },
        text.TextColor.new {
            color = Color4F.new(0.73, 0.88, 1.0, 1.0),
        },
        text.TextFont.new {font = font, font_size = 18.0}
    ):set_parent(controls:id())

    local victory_panel = commands:spawn(
        ui.Node.new {
            display = ui.Display.None,
            width = ui.Length.px(520.0),
            height = ui.Length.px(124.0),
            align_items = ui.AlignItems.Center,
            justify_content = ui.JustifyContent.Center,
        },
        ui.BackgroundColor.new {
            color = Color4F.new(0.035, 0.24, 0.19, 0.96),
        },
        ui.BorderRadius.all(ui.Length.px(18.0)),
        ArenaVictoryPanel.new()
    )
    victory_panel:set_parent(ui_root:id())

    commands:spawn(
        text.Text.new {value = "GATE CLEARED  •  +10"},
        text.TextColor.new {
            color = Color4F.new(0.76, 1.0, 0.87, 1.0),
        },
        text.TextFont.new {font = font, font_size = 38.0}
    ):set_parent(victory_panel:id())

    state.initialized = 1
    state.player = player:id()
    state.enemy = enemy:id()
    state.physics_probe = physics_probe:id()
    state.enemy_alive = 1
end

local function handle_actions(
    commands: Commands,
    input: ResRO<KeyInput>,
    time: ResRO<Time>,
    state: ResRW<CheckpointArenaState>,
    players: Query<
        Entity,
        Write<CheckpointArenaPlayer>,
        Write<Transform2d>
    >,
    enemies: Query<
        Entity,
        Write<CheckpointArenaActor>,
        Write<Transform2d>,
        Without<CheckpointArenaPlayer>
    >
)
    local keyboard_horizontal = 0.0
    if input:pressed(KeyCode.A) or input:pressed(KeyCode.Left) then
        keyboard_horizontal -= 1.0
    end
    if input:pressed(KeyCode.D) or input:pressed(KeyCode.Right) then
        keyboard_horizontal += 1.0
    end

    local horizontal = math.clamp(
        state.move_axis + keyboard_horizontal,
        -1.0,
        1.0
    )
    local keyboard_move_started =
        input:just_pressed(KeyCode.A) or
        input:just_pressed(KeyCode.Left) or
        input:just_pressed(KeyCode.D) or
        input:just_pressed(KeyCode.Right)
    local jump_requested = state.jump_requested ~= 0 or
        input:just_pressed(KeyCode.W) or
        input:just_pressed(KeyCode.Up)
    local attack_power = state.attack_requested
    if input:just_pressed(KeyCode.Space) and state.enemy_alive ~= 0 then
        attack_power = 3
    end

    local action_count = 0
    if state.move_action_started ~= 0 or keyboard_move_started then
        action_count += 1
    end
    state.move_action_started = 0
    state.jump_requested = 0
    state.attack_requested = 0

    local player_x = 0.0
    local delta = time:delta()
    for entity, player, transform in players do
        if entity == state.player then
            transform.position.x += horizontal * player.speed * delta
            transform.position.x = math.clamp(
                transform.position.x,
                -4.15,
                4.15
            )
            if horizontal < 0.0 then
                transform.scale.x = -1.0
            elseif horizontal > 0.0 then
                transform.scale.x = 1.0
            end

            if jump_requested and player.grounded ~= 0 then
                player.velocity_y = 5.2
                player.grounded = 0
                action_count += 1
            end
            player.velocity_y -= 13.0 * delta
            transform.position.y += player.velocity_y * delta
            if transform.position.y <= -1.15 then
                transform.position.y = -1.15
                player.velocity_y = 0.0
                player.grounded = 1
            end
            player_x = transform.position.x
        end
    end

    if attack_power > 0 and state.enemy_alive ~= 0 then
        action_count += 1
        for entity, actor, transform in enemies do
            local in_range = math.abs(transform.position.x - player_x) <= 1.45
            if entity == state.enemy and in_range then
                actor.health -= attack_power
            end
            if actor.health <= 0 then
                actor.health = 0
                commands:entity(entity):despawn()
                state.enemy = nil
                state.enemy_alive = 0
                state.score = 10
            end
        end
    end

    state.turn += action_count
    state.actions += action_count
end

local function sync_actor_visuals(
    actors: Query<Read<CheckpointArenaActor>, Write<Sprite>>
)
    for actor, sprite in actors do
        if actor.kind == 1 then
            sprite.size = Vector2.new(1.15, 1.35)
            sprite.color = Color4F.new(1.0, 1.0, 1.0, 1.0)
        else
            local scale = 0.82 + actor.health * 0.03
            sprite.size = Vector2.new(1.55 * scale, 1.05 * scale)
            sprite.color = Color4F.new(1.0, 1.0, 1.0, 1.0)
        end
    end
end

local function sync_status_ui(
    state: ResRO<CheckpointArenaState>,
    actors: Query<Read<CheckpointArenaActor>>,
    labels: Query<Write<text.Text>, With<ArenaStatusLabel>>,
    health_fills: Query<Write<ui.Node>, With<ArenaEnemyHealthFill>>,
    victory_panels: Query<Write<ui.Node>, With<ArenaVictoryPanel>>
)
    local player_health = 0
    local enemy_health = 0
    for actor in actors do
        if actor.kind == 1 then
            player_health = actor.health
        elseif actor.kind == 2 then
            enemy_health = actor.health
        end
    end

    local status = "HP " .. tostring(player_health) ..
        "   EMBER GUARDIAN " .. tostring(enemy_health) ..
        "   SCORE " .. tostring(state.score) ..
        "   ACTIONS " .. tostring(state.actions)
    for label in labels do
        if label.value ~= status then
            label.value = status
        end
    end

    for node in health_fills do
        node.width = ui.Length.percent(
            math.clamp(enemy_health / 6.0, 0.0, 1.0) * 100.0
        )
    end

    local victory_display = if state.enemy_alive == 0
        then ui.Display.Flex
        else ui.Display.None
    for node in victory_panels do
        node.display = victory_display
    end
end

export local CheckpointRenderPlugin = Plugin.new {
    build = function(app: App)
        app:add_resource(CheckpointArenaState {})
        app:add_system(StartUp, initialize)
        app:add_system(
            Update,
            chain(handle_actions, sync_actor_visuals, sync_status_ui)
        )
    end,
}
