local function update_status(state)
    if state.enemy_hp <= 0 then
        state.enemy_hp = 0
        state.status = 1
    elseif state.player_hp <= 0 then
        state.player_hp = 0
        state.status = 2
    end
end

local function enemy_turn(state)
    state.player_hp -= 2
    state.turn += 1
    state.energy = 3
    update_status(state)
end

local function play_card(state, card, target)
    local cost = 0
    local damage = 0
    local healing = 0
    local expected_target = "enemy"
    if card == "strike" then
        cost = 1
        damage = 2
    elseif card == "heavy" then
        cost = 2
        damage = 4
    elseif card == "heal" then
        cost = 1
        healing = 3
        expected_target = "self"
    else
        error("unknown card '" .. tostring(card) .. "'")
    end

    if target ~= expected_target then
        error("card '" .. card .. "' requires target '" .. expected_target .. "'")
    end
    if cost > state.energy then
        error("not enough energy for card '" .. card .. "'")
    end

    state.energy -= cost
    state.enemy_hp -= damage
    state.player_hp = math.min(10, state.player_hp + healing)
    update_status(state)
    if state.status == 0 and state.energy == 0 then
        enemy_turn(state)
    end
end

local function begin_step(ctx, action)
    local state = ctx:resource(CardBattleState)
    if state.status ~= 0 then
        error("the card battle is already finished")
    end

    if action.kind == "play_card" then
        play_card(state, action.card, action.target)
    elseif action.kind == "end_turn" then
        if action.card ~= "none" or action.target ~= "none" then
            error("end_turn requires card and target to be 'none'")
        end
        enemy_turn(state)
    else
        error("unsupported card action '" .. tostring(action.kind) .. "'")
    end
    state.actions += 1
end

local function status_name(status)
    if status == 1 then
        return "won"
    elseif status == 2 then
        return "lost"
    end
    return "playing"
end

local function observe(ctx)
    local state = ctx:resource(CardBattleState)
    return {
        turn = state.turn,
        player_hp = state.player_hp,
        enemy_hp = state.enemy_hp,
        energy = state.energy,
        actions = state.actions,
        status = status_name(state.status),
        hand = {
            {
                id = "strike",
                cost = 1,
                damage = 2,
                healing = 0,
                target = "enemy",
                available = state.energy >= 1,
            },
            {
                id = "heavy",
                cost = 2,
                damage = 4,
                healing = 0,
                target = "enemy",
                available = state.energy >= 2,
            },
            {
                id = "heal",
                cost = 1,
                damage = 0,
                healing = 3,
                target = "self",
                available = state.energy >= 1,
            },
        },
    }
end

return playtest {
    id = "card-battle.main",
    label = "Card Battle",
    description = "Accepts semantic turn actions instead of device input. " ..
        "Choose cards from the observed hand and reduce enemy_hp to zero.",

    types = {
        CardBattleState = "sample.card_battle_test.CardBattleState",
    },

    ticks = {
        default = 1,
        min = 1,
        max = 1,
        overridable = false,
    },

    action = {
        type = "object",
        properties = {
            kind = {
                type = "string",
                enum = {"play_card", "end_turn"},
            },
            card = {
                type = "string",
                enum = {"strike", "heavy", "heal", "none"},
            },
            target = {
                type = "string",
                enum = {"enemy", "self", "none"},
            },
        },
        required = {"kind", "card", "target"},
        additionalProperties = false,
    },

    observation = {
        type = "object",
        properties = {
            turn = {type = "integer"},
            player_hp = {type = "integer"},
            enemy_hp = {type = "integer"},
            energy = {type = "integer"},
            actions = {type = "integer"},
            status = {
                type = "string",
                enum = {"playing", "won", "lost"},
            },
            hand = {type = "array"},
        },
        required = {
            "turn",
            "player_hp",
            "enemy_hp",
            "energy",
            "actions",
            "status",
            "hand",
        },
        additionalProperties = false,
    },

    begin_step = begin_step,
    observe = observe,
}
