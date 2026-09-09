return function(ctx)
    local s = ctx.observation
    if s.won == 1 then return {stop="won"} end
    return {action={horizontal=1, jump=s.x >= 0.15 and s.grounded == 1, interact=false}}
end
