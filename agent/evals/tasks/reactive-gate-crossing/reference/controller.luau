return function(ctx)
    local s = ctx.observation
    if s.won == 1 then return {stop="won"} end
    local next_x = s.x + 4/60
    local wait = next_x >= 1.2 and next_x <= 1.8 and not s.gate_open
    return {action={horizontal=if wait then 0 else 1, jump=false, interact=false}}
end