# Short reactive controllers

Discover the interface before writing a controller. The existing isolated Luau VM accepts source that returns one function. Each invocation receives read-only `ctx.tick` (completed ticks in this segment) and `ctx.observation` (current game state), and returns either `{action = ...}` or `{stop = "reason"}`.

The runtime invokes the controller each fixed tick, validates the action against the game schema, and clears action-owned input after the tick. Model reasoning should choose a short-term strategy; the controller handles the immediate feedback. Do not implement a busy loop or wait inside the function.

Use a conservative external tick cap appropriate to the objective. Stop early on confirmed victory/defeat, the requested condition, or a meaningful change requiring reconsideration, such as new threats or damage. Read current tool schemas: native uses `max_ticks`; browser tools use `maxTicks`.

This example is **only for an interface that advertises `won`, `lost`, `player_health`, and the `move_x`, `move_y`, `fire` action fields**, such as the current Skyline Strike contract. It illustrates interruption, not a winning strategy. Adapt the fields and action from discovery for other games.

```luau
local initial_health = nil
return function(ctx)
    local observation = ctx.observation
    if observation.won then return { stop = "objective completed" } end
    if observation.lost then return { stop = "player defeated" } end
    if initial_health == nil then
        initial_health = observation.player_health
    elseif observation.player_health < initial_health then
        return { stop = "damage taken; reassess" }
    end
    return { action = { move_x = 0, move_y = 0, fire = true } }
end
```

For a game requiring evasive movement, derive movement from observed threats; do not mistake the stationary example for a dodge controller. Closures can retain small local state between ticks within one segment, but it does not survive a new segment or game restart. The VM has source, memory, and instruction limits; do not assume project modules, filesystem access, or arbitrary engine APIs are available inside it.

Read the final reason and observation. Reaching `max_ticks` means the execution budget ended, not that the game objective succeeded. An early controller stop is only as meaningful as its observed condition. A failed controller may have already advanced some ticks; inspect current state before replacing it. Preserve error details rather than hiding a failure behind an unconditional retry.
