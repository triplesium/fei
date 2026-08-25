local Gameplay = require("./gameplay")
local Presentation = require("./presentation")

export local SkylineStrikePlugin = Plugin.new {
    dependencies = {
        Gameplay.GameplayPlugin,
        Presentation.PresentationPlugin,
    },
    build = function(_app: App)
    end,
}
