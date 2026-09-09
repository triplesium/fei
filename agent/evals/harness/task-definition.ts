import { readFileSync, statSync } from "node:fs";
import { isAbsolute, relative, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { z } from "zod";
import type { Check, EvalCase, Invoke } from "./types.js";

export interface TaskVerification {
    project: string;
    output: string;
    signal: AbortSignal;
    steps: Record<string, string>;
}
interface TaskImplementation {
    grade: EvalCase["grade"];
    reference(invoke: Invoke, step: string): Promise<string>;
    verify?(context: TaskVerification): Promise<Check[]>;
}

const schema = z.object({
    id: z.string().regex(/^[a-z0-9]+(?:-[a-z0-9]+)*$/),
    version: z.number().int().positive(),
    tags: z.array(z.string().min(1)),
    environment: z.object({
        runtime: z.enum(["native", "simulated"]),
        fixture: z.string().min(1),
        writable: z.boolean(),
        interface: z.string().optional(),
        fault: z.enum(["timeout", "start", "write"]).optional(),
    }).strict(),
    steps: z.array(z.object({ id: z.string().min(1), instruction: z.string().min(1) }).strict()).min(1),
}).strict();

export function taskDirectory(id: string): string {
    if (!/^[a-z0-9]+(?:-[a-z0-9]+)*$/.test(id)) throw new Error(`Invalid task ID: ${id}`);
    return fileURLToPath(new URL(`../tasks/${id}/`, import.meta.url));
}

export function readTaskDefinition(directory: string) {
    return schema.parse(JSON.parse(readFileSync(resolve(directory, "task.json"), "utf8")));
}

/** Data files are local to a task; executable graders remain ordinary typed TS modules. */
export function defineTask(directory: string, implementation: TaskImplementation): EvalCase {
    const definition = readTaskDefinition(directory);
    const local = (path: string) => {
        const target = resolve(directory, path);
        const fromRoot = relative(resolve(directory), target);
        if (isAbsolute(path) || fromRoot === ".." || fromRoot.startsWith("..\\") || fromRoot.startsWith("../")) {
            throw new Error(`Task asset must be inside its directory: ${path}`);
        }
        return target;
    };
    if (new Set(definition.steps.map(step => step.id)).size !== definition.steps.length) throw new Error("Duplicate step ID.");
    const instructions = definition.steps.map(step => {
        const text = readFileSync(local(step.instruction), "utf8").trim();
        if (!text) throw new Error(`Empty instruction: ${step.id}`);
        return text;
    });
    const fixture = local(definition.environment.fixture);
    if (!statSync(fixture).isDirectory()) throw new Error("Task fixture must be a directory.");
    return {
        grade: implementation.grade,
        reference: (invoke, turn = 0) => {
            const step = definition.steps[turn];
            if (!step) throw new Error(`Unknown task turn: ${turn}`);
            return implementation.reference(invoke, step.id);
        },
        verify: implementation.verify ? (project, output, signal) => implementation.verify!({
            project, output, signal,
            steps: Object.fromEntries(definition.steps.map((step, index) => [step.id, resolve(project, "..", `turn-${index + 1}`)])),
        }) : undefined,
        id: definition.id, version: definition.version, tags: definition.tags,
        steps: definition.steps.map((step, index) => ({ id: step.id, instruction: instructions[index] })),
        environment: { runtime: definition.environment.runtime, fixture },
        writable: definition.environment.writable, runtimeInterface: definition.environment.interface,
        fault: definition.environment.fault,
        files: {}, prompt: instructions[0], followUps: instructions.length > 1 ? instructions.slice(1) : undefined,
    };
}
