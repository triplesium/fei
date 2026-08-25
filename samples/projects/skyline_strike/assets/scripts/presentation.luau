local core = require("@entisium/core")
local ui = require("@entisium/ui")
local text = require("@entisium/text")
local Gameplay = require("./gameplay")

export type ScoreLabel = {}
export type HealthLabel = {}
export type ScoreDetailLabel = {}
export type ScoreAccent = {}
export type ArmorPip = {index: i32}
export type WeaponPip = {index: i32}
export type FpsLabel = {}
export type BossLabel = {}
export type MissionLabel = {}
export type MissionDetailLabel = {}
export type PlayerHealthFill = {}
export type BossHealthFill = {}
export type BossPanel = {}
export type OutcomePanel = {}
export type OutcomeLabel = {}
export type OutcomeDetail = {}

export type PresentationState = {
    initialized: i32,
    previous_health: i32,
    previous_score: i32,
    damage_flash: f32,
    score_flash: f32,
}

export type FpsState = {
    fps_elapsed: f32,
    fps_frames: i32,
    fps_value: i32,
}

local function spawn_label(
    commands,
    parent,
    font,
    value,
    font_size,
    color
)
    return commands:spawn(
        ui.Node.new {
            height = ui.Length.px(font_size + 9.0),
        },
        text.Text.new {value = value},
        text.TextColor.new {color = color},
        text.TextFont.new {font = font, font_size = font_size}
    ):set_parent(parent:id())
end

local function spawn_pip(commands, parent, width, height, color)
    return commands:spawn(
        ui.Node.new {
            width = ui.Length.px(width),
            height = ui.Length.px(height),
        },
        ui.BackgroundColor.new {color = color},
        ui.BorderRadius.all(ui.Length.px(2.0))
    ):set_parent(parent:id())
end

local function initialize_hud(
    commands: Commands,
    assets: ResRW<AssetServer>
)
    local font = assets:load(
        text.Font,
        "fonts/Cousine-Regular.ttf"
    )
    if not assets:is_loaded(font) then
        error(assets:load_error(font) or "HUD font did not load")
    end

    local root = commands:spawn(
        ui.Node.new {
            position_type = ui.PositionType.Absolute,
            width = ui.Length.percent(100.0),
            height = ui.Length.percent(100.0),
            padding = ui.Edges.all(ui.Length.px(10.0)),
            align_items = ui.AlignItems.Center,
            justify_content = ui.JustifyContent.SpaceBetween,
        }
    )

    local top = commands:spawn(
        ui.Node.new {
            width = ui.Length.percent(100.0),
            height = ui.Length.px(88.0),
            gap = ui.Length.px(10.0),
            flex_direction = ui.FlexDirection.Row,
            align_items = ui.AlignItems.Center,
            justify_content = ui.JustifyContent.SpaceBetween,
        },
        ui.BackgroundColor.new {color = Color4F.new(0.0, 0.0, 0.0, 0.0)}
    )
    top:set_parent(root:id())

    local left = commands:spawn(
        ui.Node.new {
            width = ui.Length.px(220.0),
            height = ui.Length.px(82.0),
            padding = ui.Edges.axes(ui.Length.px(14.0), ui.Length.px(10.0)),
            gap = ui.Length.px(2.0),
        },
        ui.BackgroundColor.new {
            color = Color4F.new(0.015, 0.045, 0.085, 0.88),
        },
        ui.BorderRadius.all(ui.Length.px(10.0))
    )
    left:set_parent(top:id())
    local health_label = spawn_label(
        commands,
        left,
        font,
        "AIRFRAME  100%",
        22.0,
        Color4F.new(0.65, 0.96, 1.0, 1.0)
    )
    health_label:add(HealthLabel.new())
    local armor_pips = commands:spawn(
        ui.Node.new {
            height = ui.Length.px(7.0),
            flex_direction = ui.FlexDirection.Row,
            gap = ui.Length.px(3.0),
        }
    )
    armor_pips:set_parent(left:id())
    for index = 1, 8 do
        spawn_pip(
            commands,
            armor_pips,
            19.0,
            7.0,
            Color4F.new(0.25, 0.88, 1.0, 1.0)
        ):add(ArmorPip.new {index = index})
    end

    local performance = commands:spawn(
        ui.Node.new {
            position_type = ui.PositionType.Absolute,
            width = ui.Length.px(148.0),
            height = ui.Length.px(38.0),
            left = ui.Length.px(0.0),
            bottom = ui.Length.px(0.0),
            padding = ui.Edges.axes(
                ui.Length.px(12.0),
                ui.Length.px(7.0)
            ),
            justify_content = ui.JustifyContent.Center,
        },
        ui.BackgroundColor.new {
            color = Color4F.new(0.015, 0.045, 0.085, 0.76),
        },
        ui.BorderRadius.all(ui.Length.px(8.0))
    )
    performance:set_parent(root:id())
    local fps_label = spawn_label(
        commands,
        performance,
        font,
        "FRAME  --- FPS",
        13.0,
        Color4F.new(0.48, 0.82, 0.9, 1.0)
    )
    fps_label:add(FpsLabel.new())

    local mission = commands:spawn(
        ui.Node.new {
            width = ui.Length.px(260.0),
            height = ui.Length.px(70.0),
            padding = ui.Edges.axes(ui.Length.px(12.0), ui.Length.px(8.0)),
            align_items = ui.AlignItems.Center,
            gap = ui.Length.px(1.0),
        },
        ui.BackgroundColor.new {
            color = Color4F.new(0.035, 0.055, 0.085, 0.82),
        },
        ui.BorderRadius.all(ui.Length.px(8.0))
    )
    mission:set_parent(top:id())
    local mission_label = spawn_label(
        commands,
        mission,
        font,
        "INTERCEPT ENEMY WAVE",
        18.0,
        Color4F.new(1.0, 0.76, 0.34, 1.0)
    )
    mission_label:add(MissionLabel.new())
    local mission_detail = spawn_label(
        commands,
        mission,
        font,
        "WAVE 00/16  //  ACTIVE",
        12.0,
        Color4F.new(0.48, 0.68, 0.78, 1.0)
    )
    mission_detail:add(MissionDetailLabel.new())

    local right = commands:spawn(
        ui.Node.new {
            width = ui.Length.px(210.0),
            height = ui.Length.px(82.0),
            padding = ui.Edges.axes(ui.Length.px(14.0), ui.Length.px(9.0)),
            align_items = ui.AlignItems.End,
            gap = ui.Length.px(1.0),
        },
        ui.BackgroundColor.new {
            color = Color4F.new(0.015, 0.045, 0.085, 0.88),
        },
        ui.BorderRadius.all(ui.Length.px(10.0))
    )
    right:set_parent(top:id())
    local score_label = spawn_label(
        commands,
        right,
        font,
        "000000",
        30.0,
        Color4F.new(1.0, 0.94, 0.75, 1.0)
    )
    score_label:add(ScoreLabel.new())
    score_label:add(ScoreAccent.new())
    local score_detail = spawn_label(
        commands,
        right,
        font,
        "KILLS 00  //  HOSTILES 00",
        11.0,
        Color4F.new(0.48, 0.68, 0.78, 1.0)
    )
    score_detail:add(ScoreDetailLabel.new())

    local weapon = commands:spawn(
        ui.Node.new {
            position_type = ui.PositionType.Absolute,
            width = ui.Length.px(210.0),
            height = ui.Length.px(56.0),
            right = ui.Length.px(0.0),
            bottom = ui.Length.px(0.0),
            padding = ui.Edges.axes(ui.Length.px(12.0), ui.Length.px(7.0)),
            gap = ui.Length.px(2.0),
        },
        ui.BackgroundColor.new {
            color = Color4F.new(0.015, 0.045, 0.085, 0.76),
        },
        ui.BorderRadius.all(ui.Length.px(8.0))
    )
    weapon:set_parent(root:id())
    spawn_label(
        commands,
        weapon,
        font,
        "PLASMA LINK  //  READY",
        13.0,
        Color4F.new(0.4, 0.85, 1.0, 1.0)
    )
    local weapon_pips = commands:spawn(
        ui.Node.new {
            height = ui.Length.px(6.0),
            flex_direction = ui.FlexDirection.Row,
            gap = ui.Length.px(3.0),
        }
    )
    weapon_pips:set_parent(weapon:id())
    for index = 1, 8 do
        spawn_pip(
            commands,
            weapon_pips,
            17.0,
            6.0,
            Color4F.new(0.18, 0.6, 0.78, 0.9)
        ):add(WeaponPip.new {index = index})
    end

    local boss_panel = commands:spawn(
        ui.Node.new {
            position_type = ui.PositionType.Absolute,
            display = ui.Display.None,
            width = ui.Length.px(520.0),
            height = ui.Length.px(60.0),
            top = ui.Length.px(92.0),
            padding = ui.Edges.axes(
                ui.Length.px(14.0),
                ui.Length.px(6.0)
            ),
            gap = ui.Length.px(3.0),
            align_items = ui.AlignItems.Center,
        },
        ui.BackgroundColor.new {
            color = Color4F.new(0.12, 0.015, 0.025, 0.9),
        },
        ui.BorderRadius.all(ui.Length.px(12.0)),
        BossPanel.new()
    )
    boss_panel:set_parent(root:id())
    local boss_label = spawn_label(
        commands,
        boss_panel,
        font,
        "THREAT // DREADNOUGHT  28/28",
        15.0,
        Color4F.new(1.0, 0.55, 0.38, 1.0)
    )
    boss_label:add(BossLabel.new())
    local boss_track = commands:spawn(
        ui.Node.new {
            width = ui.Length.px(488.0),
            height = ui.Length.px(9.0),
        },
        ui.BackgroundColor.new {
            color = Color4F.new(0.16, 0.04, 0.05, 1.0),
        },
        ui.BorderRadius.all(ui.Length.px(5.0))
    )
    boss_track:set_parent(boss_panel:id())
    commands:spawn(
        ui.Node.new {
            width = ui.Length.percent(100.0),
            height = ui.Length.percent(100.0),
        },
        ui.BackgroundColor.new {
            color = Color4F.new(1.0, 0.18, 0.06, 1.0),
        },
        ui.BorderRadius.all(ui.Length.px(5.0)),
        BossHealthFill.new()
    ):set_parent(boss_track:id())

    local outcome = commands:spawn(
        ui.Node.new {
            display = ui.Display.None,
            width = ui.Length.px(500.0),
            height = ui.Length.px(180.0),
            padding = ui.Edges.all(ui.Length.px(18.0)),
            align_items = ui.AlignItems.Center,
            justify_content = ui.JustifyContent.Center,
            gap = ui.Length.px(5.0),
        },
        ui.BackgroundColor.new {
            color = Color4F.new(0.012, 0.04, 0.085, 0.95),
        },
        ui.BorderRadius.all(ui.Length.px(20.0)),
        OutcomePanel.new()
    )
    outcome:set_parent(root:id())
    spawn_label(
        commands,
        outcome,
        font,
        "OPERATION SKYLINE STRIKE",
        10.0,
        Color4F.new(1.0, 0.73, 0.3, 1.0)
    )
    local outcome_label = spawn_label(
        commands,
        outcome,
        font,
        "MISSION COMPLETE",
        38.0,
        Color4F.new(0.55, 0.95, 1.0, 1.0)
    )
    outcome_label:add(OutcomeLabel.new())
    local outcome_detail = spawn_label(
        commands,
        outcome,
        font,
        "CORRIDOR SECURED  //  SCORE 000000",
        16.0,
        Color4F.new(0.76, 0.86, 0.94, 1.0)
    )
    outcome_detail:add(OutcomeDetail.new())
    spawn_label(
        commands,
        outcome,
        font,
        "PRESS PLAY TO DEPLOY AGAIN",
        12.0,
        Color4F.new(0.38, 0.62, 0.74, 1.0)
    )
end

local function update_fps(
    time: ResRO<core.Time>,
    state: ResRW<FpsState>,
    labels: Query<
        Write<text.Text>,
        Write<text.TextColor>,
        With<FpsLabel>
    >
)
    local frame_delta = time:delta()
    if frame_delta > 0.0 then
        state.fps_elapsed += frame_delta
        state.fps_frames += 1
    end
    if state.fps_elapsed >= 0.5 then
        state.fps_value = math.floor(
            state.fps_frames / state.fps_elapsed + 0.5
        )
        state.fps_elapsed = 0.0
        state.fps_frames = 0
    end

    for label, color in labels do
        label.value = if state.fps_value > 0
            then string.format("FRAME  %03d FPS", state.fps_value)
            else "FRAME  --- FPS"
        color.color = if state.fps_value == 0 or state.fps_value >= 55
            then Color4F.new(0.48, 0.82, 0.9, 1.0)
            elseif state.fps_value >= 45
                then Color4F.new(1.0, 0.72, 0.28, 1.0)
                else Color4F.new(1.0, 0.28, 0.18, 1.0)
    end
end

local function update_hud(
    time: ResRO<core.Time>,
    state: ResRO<Gameplay.GameState>,
    presentation: ResRW<PresentationState>,
    scores: Query<Write<text.Text>, With<ScoreLabel>>,
    score_details: Query<Write<text.Text>, With<ScoreDetailLabel>>,
    score_accents: Query<Write<text.TextColor>, With<ScoreAccent>>,
    health_labels: Query<
        Write<text.Text>,
        Write<text.TextColor>,
        With<HealthLabel>
    >,
    missions: Query<Write<text.Text>, With<MissionLabel>>,
    mission_details: Query<Write<text.Text>, With<MissionDetailLabel>>,
    armor_pips: Query<Write<ui.BackgroundColor>, Read<ArmorPip>>,
    weapon_pips: Query<Write<ui.BackgroundColor>, Read<WeaponPip>>,
    boss_panels: Query<Write<ui.Node>, With<BossPanel>>,
    boss_labels: Query<Write<text.Text>, With<BossLabel>>,
    boss_fills: Query<Write<ui.Node>, With<BossHealthFill>>,
    outcome_panels: Query<Write<ui.Node>, With<OutcomePanel>>,
    outcome_labels: Query<Write<text.Text>, With<OutcomeLabel>>,
    outcome_details: Query<Write<text.Text>, With<OutcomeDetail>>
)
    if presentation.initialized == 0 then
        presentation.initialized = 1
        presentation.previous_health = state.player_health
        presentation.previous_score = state.score
    end
    if state.player_health < presentation.previous_health then
        presentation.damage_flash = 0.5
    end
    if state.score > presentation.previous_score then
        presentation.score_flash = 0.32
    end
    presentation.previous_health = state.player_health
    presentation.previous_score = state.score
    presentation.damage_flash = math.max(
        0.0,
        presentation.damage_flash - time:delta()
    )
    presentation.score_flash = math.max(
        0.0,
        presentation.score_flash - time:delta()
    )

    for label in scores do
        label.value = string.format("%06d", state.score)
    end
    for label in score_details do
        label.value = string.format(
            "KILLS %02d  //  HOSTILES %02d",
            state.kills,
            state.enemy_count
        )
    end
    for color in score_accents do
        color.color = if presentation.score_flash > 0.0
            then Color4F.new(1.0, 1.0, 1.0, 1.0)
            else Color4F.new(1.0, 0.94, 0.75, 1.0)
    end

    local health_percent = math.floor(
        state.player_health / 8.0 * 100.0 + 0.5
    )
    for label, color in health_labels do
        label.value = string.format("AIRFRAME  %03d%%", health_percent)
        color.color = if presentation.damage_flash > 0.0
            then Color4F.new(1.0, 0.25, 0.18, 1.0)
            else Color4F.new(0.65, 0.96, 1.0, 1.0)
    end
    for background, pip in armor_pips do
        if pip.index <= state.player_health then
            background.color = if presentation.damage_flash > 0.0
                then Color4F.new(1.0, 0.18, 0.08, 1.0)
                else Color4F.new(0.25, 0.88, 1.0, 1.0)
        else
            background.color = Color4F.new(0.12, 0.18, 0.23, 0.85)
        end
    end
    local pulse = math.floor(time:elapsed_time() * 7.0) % 8 + 1
    for background, pip in weapon_pips do
        background.color = if pip.index == pulse
            then Color4F.new(0.72, 0.97, 1.0, 1.0)
            else Color4F.new(0.18, 0.6, 0.78, 0.9)
    end

    local boss_visible = state.boss_spawned ~= 0 and state.phase == 0
    for node in boss_panels do
        node.display = if boss_visible then ui.Display.Flex else ui.Display.None
    end
    for label in boss_labels do
        label.value = string.format(
            "THREAT // DREADNOUGHT  %02d/28",
            state.boss_health
        )
    end
    for node in boss_fills do
        node.width = ui.Length.percent(state.boss_health / 28.0 * 100.0)
    end

    for label in missions do
        if state.phase == 1 then
            label.value = "CORRIDOR SECURED"
        elseif state.phase == 2 then
            label.value = "SIGNAL LOST"
        elseif state.boss_spawned ~= 0 then
            label.value = "BREAK THE DREADNOUGHT"
        else
            label.value = "INTERCEPT ENEMY WAVE"
        end
    end
    for label in mission_details do
        label.value = if state.phase == 0
            then string.format("WAVE %02d/16  //  ACTIVE", state.spawned)
            else "COMBAT TELEMETRY  //  CLOSED"
    end

    for node in outcome_panels do
        node.display = if state.phase == 0 then ui.Display.None else ui.Display.Flex
    end
    for label in outcome_labels do
        label.value = if state.phase == 1
            then "MISSION COMPLETE"
            else "AIRFRAME LOST"
    end
    for label in outcome_details do
        label.value = if state.phase == 1
            then string.format(
                "CORRIDOR SECURED  //  SCORE %06d",
                state.score
            )
            else string.format(
                "KILLS %02d  //  SCORE %06d",
                state.kills,
                state.score
            )
    end
end

export local PresentationPlugin = Plugin.new {
    dependencies = {Gameplay.GameplayPlugin},
    build = function(app: App)
        app:add_resource(PresentationState {})
        app:add_resource(FpsState {})
        app:add_system(StartUp, initialize_hud)
        app:add_system(Update, update_hud)
        app:add_system(Update, update_fps)
    end,
}
