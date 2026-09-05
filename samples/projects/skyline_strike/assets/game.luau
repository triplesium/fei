local Gameplay = require("./gameplay")
local Hud = require("./hud")
local Playtest = require("./playtest")

export local SkylineStrikePlugin = Plugin.new {
    dependencies = {
        Playtest.SkylineStrikePlaytestPlugin,
    },
    build = function(app: App)
        app:add_resource(Gameplay.GameState {})
        app:add_resource(Gameplay.GameAssets {})
        app:add_resource(Gameplay.ControlState {})
        app:add_resource(Hud.PresentationState {})
        app:add_resource(Hud.FpsState {})

        app:add_systems(
            StartUp,
            chain(Gameplay.initialize, Hud.initialize_hud)
        )
        app:add_system(FixedPreUpdate, Gameplay.read_keyboard)
        app:add_systems(
            FixedUpdate,
            chain(
                Gameplay.move_player,
                Gameplay.fire_player,
                Gameplay.spawn_wave,
                Gameplay.update_enemies,
                Gameplay.move_projectiles,
                Gameplay.detect_hits
            ),
            Gameplay.animate_effects,
            Gameplay.scroll_stars,
            Gameplay.update_camera
        )
        app:add_system(Update, Hud.update_hud)
        app:add_system(Update, Hud.update_fps)
    end,
}
