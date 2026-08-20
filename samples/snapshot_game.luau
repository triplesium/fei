local TEAM_PLAYER, TEAM_ENEMY = 0, 1
local ACTOR_HERO, ACTOR_GOBLIN, ACTOR_VENOM, ACTOR_ARCHER = 0, 1, 2, 3
local ACTION_MOVE, ACTION_ATTACK, ACTION_USE_POTION, ACTION_WAIT = 0, 1, 2, 3
local WIDTH, HEIGHT = 12, 8
local WALLS = {
    { x = 4, y = 2 }, { x = 4, y = 3 }, { x = 4, y = 4 },
    { x = 7, y = 1 }, { x = 7, y = 2 }, { x = 7, y = 4 }, { x = 7, y = 5 },
}

local function actor_name(kind: number): string
    if kind == ACTOR_HERO then return "hero" end
    if kind == ACTOR_GOBLIN then return "goblin" end
    if kind == ACTOR_VENOM then return "venom" end
    if kind == ACTOR_ARCHER then return "archer" end
    return "unknown"
end

local function wall(x: number, y: number): boolean
    if x < 0 or y < 0 or x >= WIDTH or y >= HEIGHT then return true end
    if x == 0 or y == 0 or x == WIDTH - 1 or y == HEIGHT - 1 then return true end
    for _, position in WALLS do
        if position.x == x and position.y == y then return true end
    end
    return false
end

local function append_log(log: ResRW<CombatLog>, message: string)
    log.last = message
    log.count += 1
end

local function next_random(rng: ResRW<DeterministicRng>, upper_bound: number): number
    rng.state = (rng.state * 48271) % 2147483647
    return rng.state % upper_bound
end

local function actor_at(
    actors: Query<Entity, Read<GridPosition>, Read<Fighter>>,
    x: number,
    y: number,
    except: number?
): number?
    for entity, position, fighter in actors do
        if (except == nil or entity ~= except) and position.x == x
            and position.y == y and fighter.hp > 0 then
            return entity
        end
    end
    return nil
end

local function setup_game(commands: Commands, state: ResRW<DungeonState>, log: ResRW<CombatLog>)
    if state.initialized then return end
    local player = commands:spawn(
        GridPosition.new { x = 1, y = 1 },
        Fighter.new { kind = ACTOR_HERO, team = TEAM_PLAYER, hp = 32, max_hp = 32, attack = 4, range = 1 },
        StatusEffects.new {}, Inventory.new { potions = 1 }
    ):id()
    commands:spawn(
        GridPosition.new { x = 2, y = 1 },
        Fighter.new { kind = ACTOR_GOBLIN, team = TEAM_ENEMY, hp = 7, max_hp = 7, attack = 2, range = 1 },
        StatusEffects.new {}, EnemyBrain.new { target = player }
    )
    commands:spawn(
        GridPosition.new { x = 2, y = 2 },
        Fighter.new { kind = ACTOR_VENOM, team = TEAM_ENEMY, hp = 8, max_hp = 8, attack = 1, range = 2 },
        StatusEffects.new {}, EnemyBrain.new { target = player }
    )
    commands:spawn(
        GridPosition.new { x = 9, y = 5 },
        Fighter.new { kind = ACTOR_ARCHER, team = TEAM_ENEMY, hp = 6, max_hp = 6, attack = 2, range = 5 },
        StatusEffects.new {}, EnemyBrain.new { target = player }
    )
    state.initialized, state.player, state.enemies_remaining = true, player, 3
    state.exit_x, state.exit_y = 10, 6
    append_log(log, "dungeon initialized by Luau")
end

local function tick_status(
    statuses: Query<Entity, Read<Fighter>, Write<StatusEffects>>,
    damage: EventWriter<DamageEvent>
)
    for entity, fighter, status in statuses do
        if status.shield > 0 then status.shield -= 1 end
        if fighter.hp > 0 and status.poison_turns > 0 then
            status.poison_turns -= 1
            damage(DamageEvent.new { source = entity, target = entity, amount = 1, poison_turns = 0 })
        end
    end
end

local function apply_player_action(
    action: ResRW<ActionQueue>, state: ResRW<DungeonState>,
    rng: ResRW<DeterministicRng>, log: ResRW<CombatLog>,
    player_rows: Filtered<Query<Entity, Write<GridPosition>, Write<Fighter>, Write<StatusEffects>, Write<Inventory>>, With<Inventory>>,
    actors: Query<Entity, Read<GridPosition>, Read<Fighter>>,
    loot_rows: Query<Entity, Read<GridPosition>, Read<Loot>>,
    commands: Commands, damage: EventWriter<DamageEvent>
)
    if not state.initialized or state.victory or state.defeat then return end
    local kind = if action.has_action then action.kind else ACTION_WAIT
    local dx, dy = action.dx, action.dy
    action.has_action = false
    for entity, position, fighter, status, inventory in player_rows do
        if entity ~= state.player or fighter.hp <= 0 then continue end
        if kind == ACTION_MOVE then
            local x, y = position.x + dx, position.y + dy
            if not wall(x, y) and actor_at(actors, x, y, entity) == nil then
                position.x, position.y = x, y
                append_log(log, "player moves")
                for loot_entity, loot_position, loot in loot_rows do
                    if loot_position.x == x and loot_position.y == y then
                        inventory.potions += 1
                        state.score += loot.healing
                        commands:entity(loot_entity):despawn()
                        append_log(log, "player collects a healing flask")
                    end
                end
            else append_log(log, "player bumps into an obstacle") end
        elseif kind == ACTION_ATTACK then
            local target = actor_at(actors, position.x + dx, position.y + dy, entity)
            if target ~= nil then
                damage(DamageEvent.new {
                    source = entity, target = target,
                    amount = fighter.attack + next_random(rng, 2), poison_turns = 0,
                })
                append_log(log, "player attacks")
            else append_log(log, "player attacks empty space") end
        elseif kind == ACTION_USE_POTION then
            if inventory.potions > 0 then
                inventory.potions -= 1
                fighter.hp = math.min(fighter.max_hp, fighter.hp + 8)
                status.shield += 2
                append_log(log, "player drinks a potion and gains a shield")
            else append_log(log, "player has no potion") end
        else append_log(log, "player waits") end
    end
end

local function enemy_ai(
    state: ResRO<DungeonState>, rng: ResRW<DeterministicRng>, log: ResRW<CombatLog>,
    players: Filtered<Query<Entity, Read<GridPosition>, Read<Fighter>>, With<Inventory>>,
    enemies: Query<Entity, Read<Fighter>, Write<GridPosition>, Read<EnemyBrain>>,
    actors: Query<Entity, Read<GridPosition>, Read<Fighter>>,
    damage: EventWriter<DamageEvent>
)
    if not state.initialized or state.victory or state.defeat then return end
    local player_x, player_y, player_alive = 0, 0, false
    for entity, position, fighter in players do
        if entity == state.player and fighter.hp > 0 then
            player_x, player_y, player_alive = position.x, position.y, true
        end
    end
    if not player_alive then return end
    for entity, fighter, position, brain in enemies do
        if fighter.hp <= 0 or fighter.team ~= TEAM_ENEMY then continue end
        if brain.target ~= state.player then
            append_log(log, "enemy lost its remapped target")
            continue
        end
        local dx, dy = player_x - position.x, player_y - position.y
        local distance = math.abs(dx) + math.abs(dy)
        local attack = distance == 1
            or (fighter.kind == ACTOR_VENOM and distance <= 2)
            or (fighter.kind == ACTOR_ARCHER and distance <= fighter.range and (dx == 0 or dy == 0))
        if attack then
            damage(DamageEvent.new {
                source = entity, target = state.player,
                amount = fighter.attack + next_random(rng, 2),
                poison_turns = if fighter.kind == ACTOR_VENOM then 3 else 0,
            })
            append_log(log, actor_name(fighter.kind) .. " attacks player")
        else
            local sx = if math.abs(dx) >= math.abs(dy) then (if dx > 0 then 1 else -1) else 0
            local sy = if sx == 0 then (if dy > 0 then 1 else -1) else 0
            local x, y = position.x + sx, position.y + sy
            if not wall(x, y) and actor_at(actors, x, y, entity) == nil then
                position.x, position.y = x, y
            end
        end
    end
end

local function resolve_damage(
    events: EventReaderRO<DamageEvent>,
    fighters: Query<Entity, Write<Fighter>, Write<StatusEffects>>,
    deaths: EventWriter<DeathEvent>, log: ResRW<CombatLog>
)
    for event in events do
        for entity, fighter, status in fighters do
            if entity ~= event.target or fighter.hp <= 0 then continue end
            local absorbed = math.min(status.shield, event.amount)
            status.shield -= absorbed
            fighter.hp -= event.amount - absorbed
            status.poison_turns = math.max(status.poison_turns, event.poison_turns)
            if event.poison_turns > 0 then append_log(log, "player is poisoned") end
            if fighter.hp <= 0 then
                deaths(DeathEvent.new { source = event.source, target = entity, kind = fighter.kind })
            end
        end
    end
end

local function resolve_deaths(
    events: EventReaderRO<DeathEvent>,
    victims: Query<Entity, Read<GridPosition>, Read<Fighter>>,
    commands: Commands, state: ResRW<DungeonState>, log: ResRW<CombatLog>
)
    for event in events do
        for entity, position, fighter in victims do
            if entity ~= event.target then continue end
            if fighter.kind == ACTOR_HERO then
                state.defeat = true
                append_log(log, "player is defeated")
            else
                commands:spawn(GridPosition.new { x = position.x, y = position.y }, Loot.new { healing = 3 })
                commands:entity(entity):despawn()
                state.score += 10
                state.enemies_remaining -= 1
                append_log(log, actor_name(event.kind) .. " is defeated")
            end
        end
    end
end

local function observe_removals(
    removed: RemovedComponents<Fighter>,
    state: ResRW<DungeonState>, telemetry: ResRW<Telemetry>
)
    for _entity in removed do
        state.removed_actors += 1
        telemetry.removed_fighters += 1
    end
end

local function observe_changes(
    changed: Filtered<Query<Entity, Read<Fighter>>, Changed<Fighter>>,
    added: Filtered<Query<Entity, Read<Loot>>, Added<Loot>>,
    telemetry: ResRW<Telemetry>
)
    for _entity, _fighter in changed do telemetry.changed_fighters += 1 end
    for _entity, _loot in added do telemetry.added_loot += 1 end
end

local function finish_turn(
    state: ResRW<DungeonState>,
    players: Filtered<Query<Entity, Read<Fighter>>, With<Inventory>>
)
    local active = false
    for entity, _fighter in players do
        if entity == state.player then active = true end
    end
    if not active or state.victory or state.defeat then return end
    state.turn += 1
    if state.enemies_remaining == 0 then state.victory = true end
end

return module {
    name = "sample.snapshot_game",
    types = {
        GridPosition = { x = field(i32, 0), y = field(i32, 0) },
        Fighter = {
            kind = field(i32, 0), team = field(i32, 0), hp = field(i32, 0),
            max_hp = field(i32, 0), attack = field(i32, 0), range = field(i32, 1),
        },
        StatusEffects = { poison_turns = field(i32, 0), shield = field(i32, 0) },
        Inventory = { potions = field(i32, 0) },
        EnemyBrain = { target = field(entity, 0) },
        Loot = { healing = field(i32, 0) },
        DamageEvent = {
            source = field(entity, 0), target = field(entity, 0),
            amount = field(i32, 0), poison_turns = field(i32, 0),
        },
        DeathEvent = { source = field(entity, 0), target = field(entity, 0), kind = field(i32, 0) },
        ActionQueue = {
            has_action = field(bool, false), kind = field(i32, 3),
            dx = field(i32, 0), dy = field(i32, 0),
        },
        DungeonMap = { width = field(i32, 12), height = field(i32, 8) },
        DungeonState = {
            initialized = field(bool, false), turn = field(i32, 0), score = field(i32, 0),
            enemies_remaining = field(i32, 0), removed_actors = field(i32, 0),
            victory = field(bool, false), defeat = field(bool, false),
            player = field(entity, 0), exit_x = field(i32, 0), exit_y = field(i32, 0),
        },
        DeterministicRng = { state = field(i32, 13579) },
        Telemetry = {
            changed_fighters = field(i32, 0), added_loot = field(i32, 0),
            removed_fighters = field(i32, 0),
        },
        CombatLog = { count = field(i32, 0), last = field(str, "") },
    },
    resources = {
        ActionQueue = {}, DungeonMap = {}, DungeonState = {},
        DeterministicRng = {}, Telemetry = {}, CombatLog = {},
    },
    systems = {
        [Update] = {
            chain(
                setup_game, tick_status, apply_player_action, enemy_ai,
                resolve_damage, resolve_deaths, observe_removals,
                observe_changes, finish_turn
            ),
        },
    },
}
