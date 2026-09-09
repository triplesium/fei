import { mkdir, readFile, writeFile } from "node:fs/promises";
import { join } from "node:path";

export const capabilityInterface = "capability.main";
export async function canonicalGameplay(): Promise<string> {
    return readFile(new URL("../fixtures/capability/gameplay.luau", import.meta.url), "utf8");
}
export interface Scenario { x: number; y: number; goal: number; obstacle: number }
export const defaultScenario: Scenario = { x: 0, y: 0, goal: 3, obstacle: 0 };

export async function writeCapabilityFixture(root: string, gameplay: string, scenario = defaultScenario) {
    await mkdir(join(root, "assets"), { recursive: true });
    await writeFile(join(root, "project.yaml"), "name: Capability Microgame\nasset_directory: assets\nplugin: project://main.luau#GamePlugin\n");
    await writeFile(join(root, "assets/gameplay.luau"), gameplay);
    await writeFile(join(root, "assets/main.luau"), mainSource(scenario));
}

function mainSource(s: Scenario) {
    return `local Gameplay = require("./gameplay")
export type CapabilityState = {
    x: f32, y: f32, vy: f32, grounded: i32, initialized: i32,
    goal: f32, won: i32, ticks: i32, score: i32, collisions: i32,
}
export type CapabilityInput = { horizontal: f32, jump: i32, interact: i32, active: i32 }
local function initialize(commands: Commands, state: ResRW<CapabilityState>)
    state.x = ${s.x}
    state.y = ${s.y}
    state.goal = ${s.goal}
    state.grounded = ${s.y === 0 ? 1 : 0}
    Gameplay.initialize(state)
    commands:spawn(Camera2d.new { vertical_size = 6.0 }, Transform2d.new())
end
local function simulate(state: ResRW<CapabilityState>, input: ResRO<CapabilityInput>)
    if input.active == 0 then return end
    local previous_x = state.x
    local previous_won = state.won
    Gameplay.simulate(state, input.horizontal, input.jump ~= 0, input.interact ~= 0)
    local blocked = false
    if ${s.obstacle} == 1 then
        blocked = state.x >= 1.2 and state.x <= 1.8 and state.y < 0.8
    elseif ${s.obstacle} == 2 then
        blocked = state.x >= 1.2 and state.x <= 1.8 and state.ticks % 8 < 4
    end
    if blocked and input.horizontal ~= 0 then
        state.x = previous_x
        state.won = previous_won
        state.collisions += 1
    end
    state.ticks += 1
end
local function begin_step(ctx, action)
    local input = ctx:resource(CapabilityInput)
    input.horizontal = action.horizontal
    input.jump = if action.jump then 1 else 0
    input.interact = if action.interact then 1 else 0
    input.active = 1
end
local function end_step(ctx)
    ctx:resource(CapabilityInput).active = 0
end
local function observe(ctx)
    local s = ctx:resource(CapabilityState)
    return { x = s.x, y = s.y, vy = s.vy, grounded = s.grounded, initialized = s.initialized,
        goal = s.goal, won = s.won, ticks = s.ticks, score = s.score, collisions = s.collisions,
        gate_open = s.ticks % 8 >= 4 }
end
export local GamePlugin = Plugin.new {
    build = function(app: App)
        app:add_resource(CapabilityState {})
        app:add_resource(CapabilityInput {})
        app:add_system(StartUp, initialize)
        app:add_system(FixedUpdate, simulate)
        app:add_playtest {
            id = "${capabilityInterface}", label = "Capability Microgame",
            description = "1/60-second ticks. horizontal moves at 4 units/s; jump launches from ground at 5 units/s; gravity is 12. interact adds one score per tick. Win at x >= goal. Obstacle mode ${s.obstacle}: 0 none; 1 wall at x in [1.2,1.8], requires y >= 0.8; 2 gate in that interval, gate_open describes whether the NEXT tick permits crossing. Wait with horizontal=0 when closed. Observing does not advance ticks.",
            ticks = { default = 1, min = 1, max = 120, overridable = true },
            action = { type = "object", properties = {
                horizontal = { type = "number", minimum = -1, maximum = 1 },
                jump = { type = "boolean" }, interact = { type = "boolean" },
            }, required = {"horizontal", "jump", "interact"}, additionalProperties = false },
            observation = { type = "object", properties = {
                x = {type="number"}, y = {type="number"}, vy = {type="number"},
                grounded = {type="integer"}, initialized = {type="integer"}, goal = {type="number"},
                won = {type="integer"}, ticks = {type="integer"}, score = {type="integer"},
                collisions = {type="integer"}, gate_open = {type="boolean"},
            }, required = {"x","y","vy","grounded","initialized","goal","won","ticks","score","collisions","gate_open"}, additionalProperties = false },
            begin_step = begin_step, end_step = end_step, observe = observe,
        }
    end,
}
`;
}
