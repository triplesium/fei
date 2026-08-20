-- Give UI text/font atlas assets one presentation-neutral warm-up tick. The
-- arena consumes no gameplay action for an unbound F1 keyboard step.
play.step("runtime.keyboard", {keys = {"F1"}})
local initial = play.observe("checkpoint-arena.main")
play.checkpoint("render-start", true)
local initial_capture = play.capture("build/checkpoint-render-initial.png")

play.step(
    "checkpoint-arena.main",
    {kind = "advance", power = 0},
    90
)
play.step("checkpoint-arena.main", {kind = "strike", power = 3})
local victory = play.step(
    "checkpoint-arena.main",
    {kind = "strike", power = 3}
).observation
assert(victory.enemy.alive == false)
local victory_checkpoint = play.checkpoint("gate-cleared", true)
local victory_capture = play.capture("build/checkpoint-render-victory.png")

play.restore("render-start")
local restored = play.observe("checkpoint-arena.main")
assert(restored.enemy.alive == true)
assert(restored.enemy.health == initial.enemy.health)
assert(restored.player.x == initial.player.x)
local restored_capture = play.capture("build/checkpoint-render-restored.png")

play.step(
    "checkpoint-arena.main",
    {kind = "jump", power = 0}
)
local alternate = play.step(
    "checkpoint-arena.main",
    {kind = "advance", power = 0},
    18
).observation
assert(alternate.player.x > initial.player.x)
assert(alternate.player.y > initial.player.y)
local alternate_capture = play.capture("build/checkpoint-render-alternate.png")

return {
    initial = initial,
    victory = victory,
    restored = restored,
    alternate = alternate,
    captures = {
        initial_capture,
        victory_capture,
        restored_capture,
        alternate_capture,
    },
    victory_checkpoint = victory_checkpoint,
}
