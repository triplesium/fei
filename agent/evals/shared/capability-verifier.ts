import { mkdir, readFile, writeFile } from "node:fs/promises";
import { join } from "node:path";
import { RuntimeSession } from "@entisium/devkit/runtime/session";
import { writeCapabilityFixture, capabilityInterface, type Scenario } from "./capability-fixture.js";
import type { Check } from "../harness/types.js";

export interface Action { horizontal: number; jump: boolean; interact: boolean }
const action = (horizontal = 0, jump = false, interact = false): Action => ({ horizontal, jump, interact });
export const probes = [
    { name: "movement forward", action: action(1), ticks: 10 },
    { name: "movement backward", action: action(-1), ticks: 5 },
    { name: "ground or air jump", action: action(0, true), ticks: 1 },
    { name: "repeated airborne jump", action: action(0, true), ticks: 1 },
    { name: "gravity and landing", action: action(), ticks: 60 },
    { name: "interaction", action: action(0, false, true), ticks: 1 },
    { name: "repeated interaction", action: action(0, false, true), ticks: 1 },
    { name: "reach goal", action: action(1), ticks: 120 },
];

export function initialState(s: Scenario) {
    return { x: s.x, y: s.y, vy: 0, grounded: s.y === 0 ? 1 : 0, goal: s.goal, won: 0, ticks: 0, score: 0, initialized: 1 };
}
export function advance(state: ReturnType<typeof initialState>, a: Action, ticks: number, speed: number, jumpSpeed: number) {
    for (let i = 0; i < ticks; i++) {
        if (a.interact) state.score++;
        if (a.jump && state.grounded) { state.vy = jumpSpeed; state.grounded = 0; }
        state.x += a.horizontal * speed / 60;
        state.vy -= 12 / 60;
        state.y += state.vy / 60;
        if (state.y <= 0) { state.y = 0; state.vy = 0; state.grounded = 1; }
        if (state.x >= state.goal) state.won = 1;
        state.ticks++;
    }
}
export function compareState(expected: ReturnType<typeof initialState>, actual: Record<string, unknown>): string[] {
    return Object.entries(expected).filter(([key, value]) => typeof actual[key] !== "number" || Math.abs((actual[key] as number) - value) > 0.002)
        .map(([key, value]) => `${key}: expected ${value}, got ${String(actual[key])}`);
}

/** Rebuild a trusted harness around the candidate module; never execute the Agent's harness. */
export async function verifyGameplay(root: string, output: string, signal: AbortSignal, speed = 4, jumpSpeed = 5): Promise<Check[]> {
    const source = await readFile(join(root, "assets/gameplay.luau"), "utf8");
    await mkdir(output, { recursive: true });
    const checks: Check[] = [];
    const transcript: unknown[] = [];
    const scenarios: Scenario[] = [ { x: 0, y: 0, goal: 3, obstacle: 0 }, { x: -2, y: 1.5, goal: 2, obstacle: 0 } ];
    for (const [index, scenario] of scenarios.entries()) {
        signal.throwIfAborted();
        const project = join(output, `scenario-${index + 1}`);
        await writeCapabilityFixture(project, source, scenario);
        const session = new RuntimeSession(undefined, async () => join(project, "project.yaml"));
        const label = `independent scenario ${index + 1}`;
        try {
            await session.invoke("runtime_play", {}, signal);
            const expected = initialState(scenario);
            const observed = await session.invoke("play_observe", { interface: capabilityInterface }, signal);
            const initial = (observed.value as any).observation;
            const initialErrors = compareState(expected, initial ?? {});
            checks.push({ name: `${label}: initialization`, passed: initialErrors.length === 0, detail: initialErrors.join("; ") });
            transcript.push({ scenario, stage: "initialization", expected: { ...expected }, actual: observed.value });
            for (const probe of probes) {
                const result = await session.invoke("play_step", { interface: capabilityInterface, action: probe.action, ticks: probe.ticks }, signal);
                advance(expected, probe.action, probe.ticks, speed, jumpSpeed);
                const errors = compareState(expected, (result.value as any).observation ?? {});
                checks.push({ name: `${label}: ${probe.name}`, passed: errors.length === 0, detail: errors.join("; ") });
                transcript.push({ scenario, probe, expected: { ...expected }, actual: result.value });
            }
        } catch (error) {
            signal.throwIfAborted();
            checks.push({ name: `${label}: execution`, passed: false, detail: `${error}\n${session.runtime.logs().text}` });
        } finally { await session.stop(); }
    }
    await writeFile(join(output, "verification.json"), JSON.stringify({ speed, jumpSpeed, checks, transcript }, null, 2));
    return checks;
}
