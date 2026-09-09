import { mkdir, mkdtemp, readFile, rm, writeFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { afterEach, describe, expect, it } from "vitest";
import { createModels, fauxAssistantMessage, fauxProvider, fauxToolCall } from "@earendil-works/pi-ai";
import { discoverTasks } from "../evals/harness/discovery.js";
import { runTrial, summarize } from "../evals/harness/runner.js";
import { regrade } from "../evals/harness/regrade.js";
import type { EvalCase } from "../evals/harness/types.js";

const cases = await discoverTasks();
const doubleSpeed = cases.find(task => task.id === "double-speed")!;

const roots: string[] = [];
afterEach(async () => { await Promise.all(roots.splice(0).map(root => rm(root, { recursive: true, force: true }))); });
async function run(task: EvalCase, extra: Partial<Parameters<typeof runTrial>[0]> = {}) {
    const root = await mkdtemp(join(tmpdir(), "entisium-evals-")); roots.push(root);
    return runTrial({ task, trial: 1, directory: join(root, "trial"), reference: true, timeoutMs: 5000, maxToolCalls: 40, ...extra });
}
function configuration(responses: ReturnType<typeof fauxAssistantMessage>[]) {
    const provider = fauxProvider(); provider.setResponses(responses);
    const models = createModels(); models.setProvider(provider.provider);
    return { model: provider.getModel(), streamFn: models.streamSimple.bind(models) };
}

describe("eval harness and graders (not real-model capability tests)", () => {
    it.each(cases.filter(c => c.environment.runtime === "simulated"))("accepts reference solution for $id", async task => {
        const result = await run(task);
        expect(result.status, result.error ?? JSON.stringify(result.checks)).toBe("passed");
        expect(result.mode).toBe("reference");
    });

    it.each(cases)("rejects empty evidence for $id", task => {
        expect(task.grade({ files: {}, calls: [], answer: "Done", state: {} }).every(c => c.passed)).toBe(false);
    });

    it("rejects unrelated changes even when requested configuration is correct", async () => {
        const task = doubleSpeed;
        const result = await run({ ...task, reference: async invoke => {
            await task.reference(invoke);
            await invoke("project_write", { path: "assets/notes.txt", content: "Changed" }); return "Done";
        } });
        expect(result.status).toBe("failed");
        expect(result.checks.find(c => c.name === "other files preserved")?.passed).toBe(false);
    });

    it("does not give the Agent credit for harness cleanup", async () => {
        const task = cases.find(c => c.id === "stop-owned-runtime")!;
        const result = await run({ ...task, reference: async invoke => {
            await invoke("native_runtime_play", {});
            await invoke("native_play_observe", { interface: "counter" }); return "position=0";
        } });
        expect(result.status).toBe("failed");
        expect(result.checks.find(c => c.name === "harness cleanup")?.passed).toBe(true);
        expect(result.checks.find(c => c.name === "agent stopped runtime")?.passed).toBe(false);
    });

    it("isolates repeated project edits", async () => {
        const first = await run(doubleSpeed); const second = await run(doubleSpeed);
        expect(first.status).toBe("passed"); expect(second.status).toBe("passed");
        expect(first.directory).not.toBe(second.directory);
    });

    it("records actual Agent events and grades the final answer", async () => {
        const result = await run(cases.find(c => c.id === "read-only-speed")!, { reference: false,
            configuration: configuration([
                fauxAssistantMessage(fauxToolCall("project_read", { path: "assets/settings.json" }), { stopReason: "toolUse" }),
                fauxAssistantMessage("speed=4"),
            ]),
        });
        expect(result.status).toBe("passed"); expect(result.mode).toBe("agent");
        expect(await readFile(join(result.directory, "trace.jsonl"), "utf8")).toContain("tool_execution_end");
    });

    it("reports model failure and still cleans up", async () => {
        const result = await run(doubleSpeed, { reference: false, configuration: configuration([
            fauxAssistantMessage("", { stopReason: "error", errorMessage: "Unavailable" }),
        ]) });
        expect(result.status).toBe("model_error");
        expect(JSON.parse(await readFile(join(result.directory, "evidence.json"), "utf8")).state.cleaned).toBe(true);
    });

    it("classifies provider output truncation as a budget failure", async () => {
        const result = await run(doubleSpeed, { reference: false, configuration: configuration([
            fauxAssistantMessage("", { stopReason: "length" }),
        ]) });
        expect(result.status).toBe("budget_exceeded");
        expect(result.stopReasons).toContain("length");
        expect(result.error).toContain("Model output limit");
    });

    it("enforces tool budgets even if a reference swallows tool errors", async () => {
        const result = await run(doubleSpeed, { maxToolCalls: 1 });
        expect(result.status).toBe("budget_exceeded"); expect(result.toolCalls).toBe(1);
    });

    it("reports grader exceptions separately", async () => {
        const result = await run({ ...doubleSpeed, grade: () => { throw new Error("Broken grader"); } });
        expect(result.status).toBe("grader_error");
    });

    it("rejects empty graders instead of passing cleanup alone", async () => {
        const result = await run({ ...doubleSpeed, grade: () => [] });
        expect(result.status).toBe("grader_error");
    });

    it("accepts equivalent JSON formatting and property order", async () => {
        const result = await run({ ...doubleSpeed, reference: async invoke => {
            await invoke("project_write", { path: "assets/settings.json", content: '{"sound":true,"lives":3,"jump":7,"speed":8}' });
            return "Updated.";
        } });
        expect(result.status).toBe("passed");
    });

    it.each(["**result=uncertain**", "`result=uncertain`", "result=uncertain."])("accepts marker presentation: %s", async answer => {
        const task = cases.find(c => c.id === "uncertain-action")!;
        const result = await run({ ...task, reference: async invoke => { await task.reference(invoke); return answer; } });
        expect(result.status).toBe("passed");
    });

    it("rejects a false confirmed marker after uncertain execution", async () => {
        const task = cases.find(c => c.id === "uncertain-action")!;
        const result = await run({ ...task, reference: async invoke => { await task.reference(invoke); return "**result=confirmed**"; } });
        expect(result.status).toBe("failed");
    });

    it("does not count actions on the old runtime as verification of newly saved code", () => {
        const task = cases.find(c => c.id === "verify-double-movement")!;
        const evidence = { files: {}, answer: "Done", state: { beforeCleanup: "stopped" }, verification: [{ name: "physics", passed: true }], calls: [
            { name: "native_runtime_play", input: {} },
            { name: "project_write", input: { path: "assets/gameplay.luau" } },
            { name: "native_play_step", input: {} },
            { name: "native_runtime_stop", input: {} },
        ] };
        expect(task.grade(evidence).find(c => c.dimension === "verification")?.passed).toBe(false);
        evidence.calls.splice(2, 0, { name: "native_runtime_stop", input: {} }, { name: "native_runtime_play", input: {} });
        expect(task.grade(evidence).find(c => c.dimension === "verification")?.passed).toBe(true);
    });

    it("distinguishes failed startup attempts from restarting after gameplay", () => {
        const task = cases.find(c => c.id === "fixed-course-win")!;
        const evidence = { files: {}, answer: "Won", state: { beforeCleanup: "stopped", observation: { observation: { won: 1, x: 3.1, ticks: 47, collisions: 0 } } }, calls: [
            { name: "native_runtime_play", input: {}, error: "Bound to another path" },
            { name: "native_runtime_play", input: {} },
            { name: "native_play_step", input: {} },
            { name: "native_runtime_stop", input: {} },
        ] };
        expect(task.grade(evidence).every(c => c.passed)).toBe(true);
        evidence.calls.push({ name: "native_runtime_play", input: {} });
        expect(task.grade(evidence).find(c => c.name === "no restart after gameplay")?.passed).toBe(false);
    });

    it("accepts a diagnostic followed by explanation, as allowed by the task", async () => {
        const task = cases.find(c => c.id === "startup-failure")!;
        const result = await run({ ...task, reference: async invoke => {
            await task.reference(invoke); return "**diagnostic=E_ASSET_42**\nA required asset is missing.";
        } });
        expect(result.status).toBe("passed");
    });

    it("reports a time budget and cleans up after cooperative cancellation", async () => {
        const result = await run({ ...doubleSpeed, reference: async invoke => {
            await new Promise(resolve => setTimeout(resolve, 30));
            await invoke("project_list", {}); return "Done";
        } }, { timeoutMs: 10 });
        expect(result.status).toBe("budget_exceeded");
        expect(JSON.parse(await readFile(join(result.directory, "evidence.json"), "utf8")).state.cleaned).toBe(true);
    });

    it("records saved-code outcome after budget exhaustion without converting it into a pass", async () => {
        const result = await run({ ...doubleSpeed, reference: async invoke => {
            await doubleSpeed.reference(invoke);
            await new Promise(resolve => setTimeout(resolve, 50)); return "Done";
        }, verify: async (_root, _output, signal) => {
            expect(signal.aborted).toBe(false);
            return [{ name: "saved outcome", passed: true, dimension: "outcome" }];
        }, grade: e => e.verification! }, { timeoutMs: 30 });
        expect(result.status).toBe("budget_exceeded");
        expect(result.checks.find(c => c.dimension === "outcome")?.passed).toBe(true);
    });

    it("grades final native state instead of an earlier successful observation", () => {
        const task = cases.find(c => c.id === "native-set-rotation")!;
        const checks = task.grade({ files: {}, answer: "Done", calls: [
            { name: "native_runtime_play", input: {} },
            { name: "native_play_step", input: { action: { rotation: 90 } } },
            { name: "native_play_observe", input: {}, value: { observation: { rotation: 90 } } },
            { name: "native_runtime_stop", input: {} },
        ], state: { observation: { observation: { rotation: 0 } } } });
        expect(checks.find(c => c.name === "verified runtime observation")?.passed).toBe(false);
    });

    it("keeps errors visible in the summary denominator", async () => {
        const pass = await run(doubleSpeed);
        const error = { ...pass, status: "environment_error" as const, trial: 2 };
        expect(summarize([pass, error])[0]).toMatchObject({ passed: 1, trials: 2, allPassed: false });
    });

    it("regrades saved evidence without rerunning tools or overwriting the original", async () => {
        const original = await run(doubleSpeed);
        const root = await mkdtemp(join(tmpdir(), "entisium-eval-regrade-")); roots.push(root);
        await writeFile(join(root, "report.json"), JSON.stringify({ results: [{ ...original, status: "failed" }] }));
        const output = join(root, "regraded"); await mkdir(output);
        const results = await regrade(root, output);
        expect(results[0].status).toBe("passed");
        expect(results[0].elapsedMs).toBe(original.elapsedMs);
        expect(JSON.parse(await readFile(join(original.directory, "result.json"), "utf8")).status).toBe("passed");
        expect(JSON.parse(await readFile(join(results[0].directory, "provenance.json"), "utf8")).sourceDirectory).toBe(original.directory);
    });
});
