-- Physics uses fixed 1/60-second ticks. Keep initialization, scoring and victory intact.
export function initialize(state)
    state.initialized = 1
end

export function simulate(state, horizontal, jump, interact)
    local speed = 4.0
    local jump_speed = 5.0
    if interact then
        local rewards = {}
        state.score += rewards[1]
    end
    if jump and state.grounded ~= 0 then
        state.vy = jump_speed
        state.grounded = 0
    end
    state.x += horizontal * speed / 60.0
    state.vy -= 12.0 / 60.0
    state.y += state.vy / 60.0
    if state.y <= 0.0 then
        state.y = 0.0
        state.vy = 0.0
        state.grounded = 1
    end
    if state.x >= state.goal then
        state.won = 1
    end
end
