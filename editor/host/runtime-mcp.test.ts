import { writeFile } from "node:fs/promises";
import { resolve } from "node:path";
import { Client } from "@modelcontextprotocol/sdk/client/index.js";
import { StdioClientTransport } from "@modelcontextprotocol/sdk/client/stdio.js";
import { expect, it } from "vitest";

// Opt in: builds are intentionally separate from tests, and all game windows are hidden.
it.skipIf(!process.env.ETS_RUNTIME_MCP_TEST_EXE)("plays a native project through stdio without the Editor or browser", async () => {
    const client = new Client({ name: "native-runtime-test", version: "1" });
    const transport = new StdioClientTransport({
        command: process.execPath,
        args: ["--import", "tsx", "host/runtime-mcp-main.ts"],
        cwd: process.cwd(), stderr: "pipe",
        env: { ...Object.fromEntries(Object.entries(process.env).filter((entry): entry is [string, string] => typeof entry[1] === "string")), ETS_RUNTIME_HOST_PATH: process.env.ETS_RUNTIME_MCP_TEST_EXE! },
    });
    let diagnostics = "";
    transport.stderr?.on("data", (data) => { diagnostics += data.toString(); });
    let gamePid: number | undefined;
    const call = async (name: string, args: Record<string, unknown> = {}) => {
        const result = await client.callTool({ name, arguments: args });
        expect(result.isError, JSON.stringify(result)).not.toBe(true);
        return result.content as Array<{ type: string; text?: string; data?: string }>;
    };
    const json = async (name: string, args: Record<string, unknown> = {}) => JSON.parse((await call(name, args))[0]!.text!);
    try {
        await client.connect(transport);
        const tools = await client.listTools();
        expect(tools.tools.map((tool) => tool.name)).toContain("play_segment");
        const project = resolve("../samples/projects/skyline_strike/project.yaml");
        gamePid = (await json("runtime_play", { project })).pid;
        const interfaces = await json("play_interfaces");
        expect(JSON.stringify(interfaces)).toContain("skyline-strike.main");
        const args = { interface: "skyline-strike.main" };
        const initial = await json("play_observe", args);
        expect(JSON.stringify(interfaces)).toContain("velocities in units/second");
        expect(initial.observation.player).toMatchObject({
            bounds: { min_x: -8, max_x: 2.7, min_y: -4.25, max_y: 4.25 },
            invincible_seconds: 0,
        });
        expect(initial.observation.player.speed).toBeCloseTo(5.7);
        expect(initial.observation.player.radius).toBeCloseTo(0.34);
        expect(initial.observation.hostile_projectiles).toEqual({});
        expect((await json("runtime_logs")).text).toContain("Runtime probe connected");
        const step = await json("play_step", { ...args, action: { move_x: 1, move_y: 0, fire: true }, ticks: 3 });
        expect(step.frame).toBe(initial.frame + 3);
        expect(step.observation.player.x, JSON.stringify({ initial, step, logs: await json("runtime_logs") })).toBeGreaterThan(initial.observation.player.x);
        const segment = await json("play_segment", { ...args, max_ticks: 10, source: `
            return function(ctx)
                if ctx.tick == 3 then return {stop = "done"} end
                return {action = {move_x = -1, move_y = 0, fire = false}}
            end
        ` });
        expect(segment.ticks).toBe(3);
        expect(segment.reason).toBe("done");
        expect(segment.frame).toBe(step.frame + 3);
        const bounded = await json("play_segment", { ...args, max_ticks: 2,
            source: "return function(ctx) return {action={move_x=0, move_y=0, fire=false}} end" });
        expect(bounded.ticks).toBe(2);
        expect(bounded.reason).toBe("max_ticks");
        const invalidAction = await client.callTool({ name: "play_step", arguments: { ...args, action: { move_x: 99, move_y: 0, fire: false }, ticks: 1 } });
        expect(invalidAction.isError).toBe(true);
        const invalid = await client.callTool({ name: "play_segment", arguments: { ...args, max_ticks: 2, source: "return function() while true do end end" } });
        expect(invalid.isError).toBe(true);
        const afterFailure = await json("play_observe", args);
        expect(afterFailure.frame).toBe(bounded.frame);
        const capture = await call("runtime_capture");
        const png = Buffer.from(capture.find((item) => item.type === "image")!.data!, "base64");
        expect(png.subarray(1, 4).toString()).toBe("PNG");
        expect(png.length).toBeGreaterThan(1000);
        if (process.env.ETS_RUNTIME_MCP_CAPTURE) await writeFile(process.env.ETS_RUNTIME_MCP_CAPTURE, png);
        expect((await json("play_observe", args)).frame).toBe(bounded.frame);
        const populated = await json("play_segment", { ...args, max_ticks: 180,
            source: "return function(ctx) return {action={move_x=0, move_y=0, fire=false}} end" });
        const enemies = Object.values(populated.observation.enemy_targets) as Array<{kind: string; health: number; radius: number}>;
        expect(enemies.length).toBeGreaterThan(0);
        expect(enemies.length).toBe(populated.observation.enemies);
        expect(enemies[0]).toMatchObject({ kind: "fighter", health: 3 });
        expect(enemies[0]!.radius).toBeCloseTo(0.47);
        const shots = populated.observation.hostile_projectiles as Record<string, {x: number; y: number; velocity_x: number; velocity_y: number; radius: number; damage: number}>;
        expect(Object.keys(shots).length).toBeGreaterThan(0);
        expect((await json("play_observe", args)).observation).toEqual(populated.observation);
        const next = await json("play_step", { ...args, action: { move_x: 0, move_y: 0, fire: false }, ticks: 1 });
        for (const [id, shot] of Object.entries(shots)) {
            expect(shot.velocity_x).toBeLessThan(0);
            expect(shot.damage).toBeGreaterThan(0);
            expect(shot.radius).toBeGreaterThan(0);
            const moved = next.observation.hostile_projectiles[id];
            expect(moved).toBeDefined();
            expect(moved.x).toBeCloseTo(shot.x + shot.velocity_x / 60, 5);
            expect(moved.y).toBeCloseTo(shot.y + shot.velocity_y / 60, 5);
        }
        await call("runtime_stop");
        expect((await json("runtime_status")).state).toBe("stopped");
        expect(() => process.kill(gamePid!, 0)).toThrow();
        gamePid = (await json("runtime_play", { project })).pid;
        // Closing stdio must also dispose the owned native process.
    } finally {
        await client.close();
    }
    await expect.poll(() => {
        try { process.kill(gamePid!, 0); return true; } catch { return false; }
    }, { timeout: 10_000 }).toBe(false);
    expect(diagnostics).not.toContain("Error:");
}, 90_000);
