import { mkdir, mkdtemp, readFile, rm, writeFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { afterEach, expect, it } from "vitest";
import { discoverTasks } from "../evals/harness/discovery.js";
import { defineTask } from "../evals/harness/task-definition.js";
import { selectCases } from "../evals/suites.js";
import { runTrial } from "../evals/harness/runner.js";
import { regrade } from "../evals/harness/regrade.js";

const cases = await discoverTasks();
const doubleSpeed = cases.find(task => task.id === "double-speed")!;

const roots: string[] = [];
afterEach(async () => { await Promise.all(roots.splice(0).map(root => rm(root, { recursive: true, force: true }))); });
async function fixture() {
    const root = await mkdtemp(join(tmpdir(), "eval-definition-")); roots.push(root);
    await mkdir(join(root, "project"));
    await writeFile(join(root, "project/project.yaml"), "name: Definition test\nasset_directory: assets\n");
    await writeFile(join(root, "first.md"), "Save the first change.\n");
    await writeFile(join(root, "second.md"), "Preserve it and add a second change.\n");
    const definition = { id: "definition-test", version: 1, tags: ["multi-turn"],
        environment: { runtime: "simulated", fixture: "project", writable: true },
        steps: [{ id: "first", instruction: "first.md" }, { id: "second", instruction: "second.md" }] };
    await writeFile(join(root, "task.json"), JSON.stringify(definition));
    return { root, definition };
}

it("runs named steps against isolated fixtures and supplies snapshots to independent verification", async () => {
    const { root } = await fixture();
    const steps: string[] = [];
    const task = defineTask(root, {
        reference: async (invoke, step) => {
            steps.push(step);
            await invoke("project_write", { path: "assets/result.txt", content: step });
            return step;
        },
        verify: async context => [{ name: "both checkpoints", dimension: "outcome", passed:
            await readFile(join(context.steps.first, "assets/result.txt"), "utf8") === "first" &&
            await readFile(join(context.steps.second, "assets/result.txt"), "utf8") === "second" }],
        grade: evidence => evidence.verification!,
    });
    const result = await runTrial({ task, trial: 1, directory: join(root, "trial"), reference: true, timeoutMs: 5000, maxToolCalls: 10 });
    expect(result.status, result.error).toBe("passed");
    expect(steps).toEqual(["first", "second"]);
    const manifest = JSON.parse(await readFile(join(result.directory, "manifest.json"), "utf8"));
    expect(manifest).toMatchObject({ taskVersion: 1, steps: [{ id: "first" }, { id: "second" }] });
    expect(await readFile(join(root, "project/project.yaml"), "utf8")).toContain("Definition test");
    const evidence = JSON.parse(await readFile(join(result.directory, "evidence.json"), "utf8"));
    expect(evidence.verification[0].passed).toBe(true);
    expect(evidence.state.verification).toBeUndefined();
});

it.each(["duplicate-step", "empty-instruction", "outside-instruction", "missing-fixture"])("rejects malformed task: %s", async fault => {
    const { root, definition } = await fixture();
    if (fault === "duplicate-step") definition.steps[1].id = "first";
    if (fault === "empty-instruction") await writeFile(join(root, "first.md"), "  ");
    if (fault === "outside-instruction") definition.steps[0].instruction = "../outside.md";
    if (fault === "missing-fixture") definition.environment.fixture = "missing";
    await writeFile(join(root, "task.json"), JSON.stringify(definition));
    expect(() => defineTask(root, { grade: () => [], reference: async () => "" })).toThrow();
});

it("selects overlapping suites without changing task environments", () => {
    const framework = selectCases(cases, { suite: "framework" });
    expect(framework).toHaveLength(3);
    expect(framework.every(task => selectCases(cases, { suite: "capability" }).includes(task))).toBe(true);
    expect(framework.every(task => task.environment.runtime === "native")).toBe(true);
    expect(selectCases(cases, {})).toHaveLength(10);
    expect(() => selectCases(cases, { suite: "unknown" })).toThrow("Unknown suite");
});

it("refuses to regrade a different task version", async () => {
    const { root } = await fixture();
    const result = await runTrial({ task: doubleSpeed, trial: 1, directory: join(root, "trial"), reference: true, timeoutMs: 5000, maxToolCalls: 10 });
    expect(result.status).toBe("passed");
    const manifestPath = join(result.directory, "manifest.json");
    const manifest = JSON.parse(await readFile(manifestPath, "utf8"));
    await writeFile(manifestPath, JSON.stringify({ ...manifest, taskVersion: 999 }));
    await writeFile(join(root, "report.json"), JSON.stringify({ results: [result] }));
    await mkdir(join(root, "regraded"));
    const [regraded] = await regrade(root, join(root, "regraded"));
    expect(regraded.status).toBe("grader_error");
    expect(regraded.error).toContain("Task version changed");
});
