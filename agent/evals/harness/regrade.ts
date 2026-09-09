import { createHash } from "node:crypto";
import { mkdir, readFile, writeFile } from "node:fs/promises";
import { join } from "node:path";
import { discoverTasks } from "./discovery.js";
import { readFiles } from "./environment.js";
import type { TrialResult } from "./types.js";

/** Re-score saved completed trials; never invoke a model or replay game actions. */
export async function regrade(source: string, output: string, verifySaved = false, signal?: AbortSignal): Promise<TrialResult[]> {
    const report = JSON.parse(await readFile(join(source, "report.json"), "utf8")) as { results: TrialResult[] };
    if (!Array.isArray(report.results) || !report.results.length) throw new Error("No saved trial results.");
    const cases = await discoverTasks();
    const results: TrialResult[] = [];
    for (const original of report.results) {
        signal?.throwIfAborted();
        const task = cases.find(c => c.id === original.caseId);
        if (!task) throw new Error(`Unknown saved case: ${original.caseId}`);
        const directory = join(output, `${task.id}-${original.trial}`);
        await mkdir(directory, { recursive: false });
        const result: TrialResult = { ...original, directory };
        if (!result.stopReasons) {
            try {
                const trace = await readFile(join(original.directory, "trace.jsonl"), "utf8");
                result.stopReasons = trace.split("\n").filter(line => line.trim()).map(line => JSON.parse(line).event)
                    .filter(event => event?.type === "message_end" && event.message?.role === "assistant")
                    .map(event => event.message.stopReason);
            } catch (error) {
                if ((error as NodeJS.ErrnoException).code !== "ENOENT") throw error;
            }
        }
        // Failures that did not reach grading are not turned into capability passes.
        const completed = ["passed", "failed"].includes(original.status);
        if (completed || (verifySaved && task.verify && ["budget_exceeded", "model_error"].includes(original.status))) {
            try {
                const manifest = JSON.parse(await readFile(join(original.directory, "manifest.json"), "utf8"));
                if (manifest.taskVersion !== undefined && manifest.taskVersion !== (task.version ?? 1)) throw new Error("Task version changed; run a fresh trial.");
                if (manifest.environment && JSON.stringify(manifest.environment) !== JSON.stringify(task.environment)) throw new Error("Task environment changed; run a fresh trial.");
                if (manifest.prompt !== task.prompt) throw new Error("Task prompt changed; run a fresh trial.");
                if (JSON.stringify(manifest.followUps ?? []) !== JSON.stringify(task.followUps ?? [])) throw new Error("Follow-up prompts changed; run a fresh trial.");
                const evidence = JSON.parse(await readFile(join(original.directory, "evidence.json"), "utf8"));
                // Read pre-framework evidence without changing the original artifact.
                evidence.verification ??= evidence.state.verification;
                if (verifySaved && task.verify) {
                    evidence.verification = await task.verify(join(original.directory, "project"), join(directory, "verification"),
                        AbortSignal.any([signal ?? new AbortController().signal, AbortSignal.timeout(120_000)]));
                    await writeFile(join(directory, "evidence.json"), JSON.stringify(evidence, null, 2));
                }
                const encoded = await readFiles(join(original.directory, "project"));
                const files = Object.fromEntries(Object.entries(encoded).map(([p, s]) => [p, Buffer.from(s, "base64").toString("utf8")]));
                result.checks = task.grade({ ...evidence, files });
                if (!result.checks.length) throw new Error("No grading checks.");
                if (!task.writable) result.checks.push({ name: "fixture unchanged", passed:
                    createHash("sha256").update(JSON.stringify(encoded)).digest("hex") === manifest.fixtureHash });
                result.checks.push({ name: "harness cleanup", passed: evidence.state.cleaned === true });
                if (completed) {
                    result.status = result.checks.every(c => c.passed) ? "passed" : "failed";
                    delete result.error;
                }
            } catch (error) {
                signal?.throwIfAborted();
                if (completed) { result.status = "grader_error"; result.error = String(error); }
                else result.error = `${original.error ?? original.status}; saved verification failed: ${error}`;
            }
        }
        if (result.stopReasons?.includes("length") && !["environment_error", "grader_error"].includes(result.status)) {
            result.status = "budget_exceeded";
            result.error = "Model output limit reached (stopReason=length).";
        }
        await writeFile(join(directory, "provenance.json"), JSON.stringify({
            sourceDirectory: original.directory, originalStatus: original.status,
            verifiedSavedProject: verifySaved && !!task.verify,
            graderHash: createHash("sha256").update(task.grade.toString()).digest("hex"),
            note: "No new model execution. Original elapsed time and token metrics retained; optional saved-project verification has separate artifacts.",
        }, null, 2));
        await writeFile(join(directory, "result.json"), JSON.stringify(result, null, 2));
        results.push(result);
    }
    return results;
}
