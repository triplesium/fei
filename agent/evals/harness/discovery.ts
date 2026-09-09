import { access, readdir } from "node:fs/promises";
import { join, resolve } from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";
import { readTaskDefinition } from "./task-definition.js";
import type { EvalCase } from "./types.js";

/** Task modules are trusted local evaluation code, never Agent-produced artifacts. */
export async function discoverTasks(directory = fileURLToPath(new URL("../tasks/", import.meta.url))): Promise<EvalCase[]> {
    const entries = (await readdir(directory, { withFileTypes: true })).filter(entry => entry.isDirectory())
        .sort((a, b) => a.name.localeCompare(b.name));
    const definitions = [];
    const ids = new Set<string>();
    for (const entry of entries) {
        const root = join(directory, entry.name);
        try { await access(join(root, "task.json")); }
        catch (error) {
            if ((error as NodeJS.ErrnoException).code === "ENOENT") continue;
            throw error;
        }
        const definition = readTaskDefinition(root);
        if (ids.has(definition.id)) throw new Error(`Duplicate task ID: ${definition.id}`);
        ids.add(definition.id);
        definitions.push({ root, name: entry.name, definition });
    }
    const tasks: EvalCase[] = [];
    for (const { root, name, definition } of definitions) {
        if (definition.id !== name) throw new Error(`Task directory ${name} does not match ID ${definition.id}.`);
        const module = await import(pathToFileURL(join(root, "index.ts")).href);
        const task = module.task as EvalCase | undefined;
        if (!task || typeof task !== "object" || typeof task.grade !== "function" || typeof task.reference !== "function") {
            throw new Error(`Task ${name} must export a task with grade and reference functions.`);
        }
        if (task.id !== definition.id || task.version !== definition.version) throw new Error(`Task export does not match manifest: ${name}`);
        if (typeof task.prompt !== "string" || !task.prompt.trim() || !task.environment ||
            task.environment.runtime !== definition.environment.runtime ||
            task.environment.fixture !== resolve(root, definition.environment.fixture) ||
            task.writable !== definition.environment.writable || task.fault !== definition.environment.fault ||
            task.runtimeInterface !== definition.environment.interface ||
            (task.verify !== undefined && typeof task.verify !== "function")) {
            throw new Error(`Task export has invalid configuration: ${name}`);
        }
        tasks.push(task);
    }
    return tasks;
}
