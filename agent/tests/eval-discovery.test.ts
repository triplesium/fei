import { mkdir, mkdtemp, rm, writeFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { afterEach, expect, it } from "vitest";
import { discoverTasks } from "../evals/harness/discovery.js";
import { selectCases } from "../evals/suites.js";

const roots: string[] = [];
afterEach(async () => { await Promise.all(roots.splice(0).map(root => rm(root, { recursive: true, force: true }))); });
async function workspace() {
    const root = await mkdtemp(join(tmpdir(), "eval-discovery-")); roots.push(root); return root;
}
async function task(root: string, name: string, id = name, code?: string) {
    const directory = join(root, name);
    await mkdir(join(directory, "project"), { recursive: true });
    await writeFile(join(directory, "instruction.md"), "A sample task.");
    await writeFile(join(directory, "task.json"), JSON.stringify({ id, version: 1, tags: [],
        environment: { runtime: "simulated", fixture: "project", writable: false },
        steps: [{ id: "run", instruction: "instruction.md" }] }));
    await writeFile(join(directory, "index.ts"), code ?? `export const task = {
        id: ${JSON.stringify(id)}, version: 1, prompt: "A sample task.", writable: false, files: {},
        environment: { runtime: "simulated", fixture: ${JSON.stringify(join(directory, "project"))} },
        grade: () => [{name: "sample", passed: true}], reference: async () => "Done"
    };`);
}

it("finds new tasks without a registry and sorts independently of creation order", async () => {
    const root = await workspace();
    await task(root, "z-last"); await task(root, "a-first");
    await mkdir(join(root, "not-a-task"));
    expect((await discoverTasks(root)).map(task => task.id)).toEqual(["a-first", "z-last"]);
    await task(root, "new-task");
    expect((await discoverTasks(root)).map(task => task.id)).toEqual(["a-first", "new-task", "z-last"]);
});

it.each(["duplicate", "directory", "export", "identity", "configuration", "missing-entry", "manifest"])("rejects invalid discovery: %s", async fault => {
    const root = await workspace();
    if (fault === "duplicate") { await task(root, "one", "same"); await task(root, "two", "same"); }
    if (fault === "directory") await task(root, "directory-name", "other-id");
    if (fault === "export") await task(root, "invalid", "invalid", "export const unrelated = {};");
    if (fault === "identity") await task(root, "invalid", "invalid", 'export const task = { id: "other", grade() {}, reference() {} };');
    if (fault === "configuration") await task(root, "invalid", "invalid", 'export const task = { id: "invalid", version: 1, grade() {}, reference() {} };');
    if (fault === "missing-entry") { await task(root, "invalid"); await rm(join(root, "invalid/index.ts")); }
    if (fault === "manifest") { await task(root, "invalid"); await writeFile(join(root, "invalid/task.json"), "{}"); }
    await expect(discoverTasks(root)).rejects.toThrow();
});

it("discovers the whole repository task collection and preserves suite selection", async () => {
    const tasks = await discoverTasks();
    expect(tasks.map(task => task.id)).toEqual(expect.arrayContaining(["double-speed", "repair-air-jump", "native-observe"]));
    expect(selectCases(tasks, { suite: "capability" })).toHaveLength(8);
    expect(selectCases(tasks, { suite: "framework" })).toHaveLength(3);
    expect(tasks.every(task => task.steps?.length && task.environment.fixture)).toBe(true);
});
