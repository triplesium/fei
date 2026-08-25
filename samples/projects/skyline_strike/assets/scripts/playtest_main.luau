local Gameplay = require("./gameplay")
local Presentation = require("./presentation")
local Playtest = require("./playtest")

export local SkylineStrikePlaytestPlugin = Plugin.new {
    dependencies = {
        Gameplay.GameplayPlugin,
        Presentation.PresentationPlugin,
        Playtest.PlaytestPlugin,
    },
    build = function(_app: App)
    end,
}
