import type { Check, Evidence } from "../harness/types.js";
import { readFileSync, readdirSync } from "node:fs";
import { join } from "node:path";

/** Read the trusted initial fixture, never the candidate's modified project. */
export function readTextFixture(root: string, prefix = ""): Record<string, string> {
    const files: Record<string, string> = {};
    for (const entry of readdirSync(join(root, prefix), { withFileTypes: true })) {
        const path = prefix ? `${prefix}/${entry.name}` : entry.name;
        if (entry.isDirectory()) Object.assign(files, readTextFixture(root, path));
        else if (entry.isFile()) files[path] = readFileSync(join(root, path), "utf8");
        else throw new Error(`Unexpected fixture entry: ${path}`);
    }
    return files;
}

export const check = (name: string, passed: boolean): Check => ({ name, passed });
export const called = (e: Evidence, name: string) => e.calls.some(c => c.name === name && !c.error);
// Presentation markup is not part of the task's semantic answer.
export const marker = (answer: string, expected: string) => answer.replace(/`|\*|__/g, "").trim().replace(/[.。]$/, "").trim().endsWith(expected);
export const unchanged = (files: Record<string, string>, e: Evidence) =>
    Object.keys(e.files).length === Object.keys(files).length && Object.entries(files).every(([p, s]) => e.files[p] === s);

export function findRotation(value: unknown): number | undefined {
    if (!value || typeof value !== "object") return undefined;
    const record = value as Record<string, unknown>;
    if (typeof record.rotation === "number") return record.rotation;
    for (const key of ["observation", "value", "payload"]) {
        const found = findRotation(record[key]);
        if (found !== undefined) return found;
    }
    return undefined;
}
