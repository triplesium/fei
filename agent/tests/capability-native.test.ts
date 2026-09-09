import { cp, mkdir, mkdtemp, rm, writeFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { afterEach, describe, expect, it } from "vitest";
import { discoverTasks } from "../evals/harness/discovery.js";
import { selectCases } from "../evals/suites.js";
import { canonicalGameplay, writeCapabilityFixture } from "../evals/shared/capability-fixture.js";
import { verifyGameplay } from "../evals/shared/capability-verifier.js";
import { regrade } from "../evals/harness/regrade.js";

const cases = await discoverTasks();

const gameplayCases = selectCases(cases, { suite: "capability" });
const roots: string[] = [];
afterEach(async () => { await Promise.all(roots.splice(0).map(root => rm(root, { recursive: true, force: true }))); });
async function workspace() {
    const root = await mkdtemp(join(tmpdir(), "entisium-capability-")); roots.push(root);
    await mkdir(join(root, "project")); return root;
}

describe.skipIf(process.env.ETS_EVAL_NATIVE_TESTS !== "1")("native capability grader negative controls", () => {
    it.each(gameplayCases.filter(c => c.writable && !c.followUps))("rejects the uncorrected $id fixture", async task => {
        const root = await workspace();
        await cp(task.environment.fixture!, join(root, "project"), { recursive: true });
        const checks = await task.verify!(join(root, "project"), join(root, "verification"), AbortSignal.timeout(60_000));
        expect(checks.some(c => !c.passed)).toBe(true);
    }, 65_000);

    it("rejects a second turn that loses the first turn's speed change", async () => {
        const root = await workspace();
        const source = await canonicalGameplay();
        await writeCapabilityFixture(join(root, "turn-1"), source.replace("speed = 4.0", "speed = 8.0"));
        await writeCapabilityFixture(join(root, "project"), source.replace("jump_speed = 5.0", "jump_speed = 6.0"));
        const task = gameplayCases.find(c => c.followUps)!;
        const checks = await task.verify!(join(root, "project"), join(root, "verification"), AbortSignal.timeout(60_000));
        expect(checks.filter(c => c.name.startsWith("first turn:")).every(c => c.passed)).toBe(true);
        expect(checks.filter(c => !c.name.startsWith("first turn:")).some(c => !c.passed)).toBe(true);
    }, 65_000);

    it("does not trust a candidate harness that fabricates observations", async () => {
        const root = await workspace();
        await writeCapabilityFixture(join(root, "project"), await canonicalGameplay());
        await writeFile(join(root, "project/assets/main.luau"), "-- Fake observations must never be executed by verification\n");
        const checks = await verifyGameplay(join(root, "project"), join(root, "verification"), AbortSignal.timeout(60_000), 8, 5);
        expect(checks.some(c => c.name.includes("movement") && !c.passed)).toBe(true);
    }, 65_000);

    it("verifies code from a timed-out run while preserving its timeout status", async () => {
        const root = await workspace();
        const task = gameplayCases.find(c => c.id === "verify-double-movement")!;
        await writeCapabilityFixture(join(root, "project"), (await canonicalGameplay()).replace("speed = 4.0", "speed = 8.0"));
        await writeFile(join(root, "manifest.json"), JSON.stringify({ prompt: task.prompt, followUps: [] }));
        await writeFile(join(root, "evidence.json"), JSON.stringify({ calls: [], answer: "", state: { cleaned: true, beforeCleanup: "stopped" } }));
        await writeFile(join(root, "report.json"), JSON.stringify({ results: [{
            caseId: task.id, trial: 1, mode: "agent", status: "budget_exceeded", error: "Time budget exceeded",
            checks: [], elapsedMs: 180000, toolCalls: 0, tokens: null, directory: root,
        }] }));
        const output = join(root, "regraded"); await mkdir(output);
        const [result] = await regrade(root, output, true);
        expect(result.status).toBe("budget_exceeded");
        expect(result.checks.filter(c => c.dimension === "outcome").every(c => c.passed)).toBe(true);
        expect(result.checks.some(c => c.dimension === "outcome")).toBe(true);
        expect(result.elapsedMs).toBe(180000);
    }, 65_000);
});
