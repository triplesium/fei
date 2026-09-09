import { parseArgs } from "node:util";
import { mkdir, writeFile } from "node:fs/promises";
import { resolve, join } from "node:path";
import { randomUUID } from "node:crypto";
import { discoverTasks } from "./harness/discovery.js";
import { selectCases } from "./suites.js";
import { runTrial, summarize } from "./harness/runner.js";
import { regrade } from "./harness/regrade.js";
import { HostModelRegistry } from "@entisium/agent/models/model-registry";
import { EncryptedCredentialStore } from "@entisium/agent/models/credential-store";
import { FileEditorModelSettingsStore } from "@entisium/agent/models/model-settings-store";
import type { AgentConfiguration } from "@entisium/agent/core/agent";
import type { TrialResult } from "./harness/types.js";

function positive(value: string | undefined, fallback: number, name: string): number {
    const n = value === undefined ? fallback : Number(value);
    if (!Number.isSafeInteger(n) || n < 1) throw new Error(`${name} must be a positive integer.`);
    return n;
}
async function main() {
    const { values } = parseArgs({ options: {
        list: { type: "boolean" }, validate: { type: "boolean" }, help: { type: "boolean" }, regrade: { type: "string" },
        "verify-saved": { type: "boolean" },
        suite: { type: "string" }, case: { type: "string" }, trials: { type: "string" },
        output: { type: "string" }, provider: { type: "string" }, model: { type: "string" },
        "timeout-ms": { type: "string" }, "max-tool-calls": { type: "string" },
    } });
    if (values.help) {
        console.log(`Usage: npm run agent:eval -- [options]
  --list                    List cases; no model or runtime required
  --validate                Run trusted reference solutions, NOT an Agent score
  --regrade <run-directory>  Re-score saved trials without model calls; write a new report
  --verify-saved            With --regrade, independently replay saved projects
  --suite project|behavior|native|capability|framework|all  Default: project and behavior
  --case <id>               Select one case (including native)
  --trials <n>              Default: 3 for Agent, 1 for references
  --provider <id> --model <id>  Override saved Editor model
  --timeout-ms <n>          Per-trial cooperative deadline (default 120000)
  --max-tool-calls <n>      Per-trial limit (default 40)
  --output <directory>      Parent of a unique run directory
Native cases require a built entisium-runtime-host; ETS_RUNTIME_HOST_PATH overrides it.
Reports and isolated project copies are retained under .entisium/evals by default.`);
        return;
    }
    if (values.regrade) {
        if (Object.keys(values).some(key => !["regrade", "output", "verify-saved"].includes(key))) throw new Error("--regrade only accepts --output and --verify-saved as additional options.");
        const directory = resolve(values.output ?? ".entisium/evals", randomUUID());
        await mkdir(directory, { recursive: true });
        console.log(`Regrading: ${directory}`);
        const controller = new AbortController();
        const cancel = () => controller.abort(new Error("Regrading interrupted."));
        process.once("SIGINT", cancel); process.once("SIGTERM", cancel);
        let results: TrialResult[];
        try { results = await regrade(resolve(values.regrade), directory, values["verify-saved"], controller.signal); }
        finally { process.removeListener("SIGINT", cancel); process.removeListener("SIGTERM", cancel); }
        await writeFile(join(directory, "report.json"), JSON.stringify({ version: 1, mode: "regrade", verifySaved: !!values["verify-saved"], source: resolve(values.regrade), summary: summarize(results), results }, null, 2));
        await writeReport(directory, results, values["verify-saved"] ? "Reverified saved projects — no new model execution" : "Regraded saved trials — no new model execution");
        if (results.some(r => r.status !== "passed")) process.exitCode = 1;
        return;
    }
    if (values["verify-saved"]) throw new Error("--verify-saved requires --regrade.");
    if (Boolean(values.provider) !== Boolean(values.model)) throw new Error("Supply both --provider and --model.");
    const selected = selectCases(await discoverTasks(), values);
    if (!selected.length) throw new Error("No matching eval cases.");
    if (values.list) { console.log(selected.map(c => `${c.id}\t${c.environment.runtime}\t${c.prompt}`).join("\n")); return; }
    const trials = positive(values.trials, values.validate ? 1 : 3, "trials");
    const timeoutMs = positive(values["timeout-ms"], 120_000, "timeout-ms");
    const maxToolCalls = positive(values["max-tool-calls"], 40, "max-tool-calls");
    let configuration: AgentConfiguration | undefined;
    if (!values.validate) {
        const registry = new HostModelRegistry(new EncryptedCredentialStore(), new FileEditorModelSettingsStore());
        const model = values.provider && values.model ? await registry.getModel(values.provider, values.model) : (await registry.activeModel()).model;
        if (!model) throw new Error("No configured model. Configure the Editor model or use --validate for reference checks.");
        configuration = { model, streamFn: registry.streamSimple.bind(registry) };
    }
    const directory = resolve(values.output ?? ".entisium/evals", randomUUID());
    await mkdir(directory, { recursive: true });
    console.log(`${values.validate ? "REFERENCE VALIDATION (not an Agent score)" : "AGENT EVAL"}: ${directory}`);
    const controller = new AbortController();
    const stop = () => controller.abort(new Error("Eval interrupted."));
    process.once("SIGINT", stop); process.once("SIGTERM", stop);
    const results: TrialResult[] = [];
    try {
        for (const task of selected) {
            for (let trial = 1; trial <= trials && !controller.signal.aborted; trial++) {
                console.log(`Running ${task.id} ${trial}/${trials}`);
                const result = await runTrial({ task, trial, directory: join(directory, `${task.id}-${trial}`),
                    configuration, reference: values.validate, timeoutMs, maxToolCalls, signal: controller.signal });
                results.push(result);
                console.log(`${result.status}: ${task.id} (${result.elapsedMs} ms)${result.error ? ` ${result.error}` : ""}`);
                await writeFile(join(directory, "report.json"), JSON.stringify({ version: 1, mode: values.validate ? "reference" : "agent",
                    requestedTrials: trials, summary: summarize(results), results }, null, 2));
            }
            if (controller.signal.aborted) break;
        }
    } finally { process.removeListener("SIGINT", stop); process.removeListener("SIGTERM", stop); }
    await writeReport(directory, results, values.validate ? "Reference validation — not an Agent score" : "Agent eval");
    if (controller.signal.aborted || results.some(r => r.status !== "passed")) process.exitCode = 1;
}

async function writeReport(directory: string, results: TrialResult[], title: string) {
    const summary = summarize(results);
    const markdown = [`# ${title}`, "",
        "| Case | Passed / trials | Outcome | Agent verification | All passed | Mean ms | Statuses |", "| --- | --- | --- | --- | --- | --- | --- |",
        ...summary.map(s => {
            const dimension = (key: string) => s.dimensions[key].total ? `${s.dimensions[key].passed}/${s.dimensions[key].total}` : "—";
            return `| ${s.caseId} | ${s.passed}/${s.trials} | ${dimension("outcome")} | ${dimension("verification")} | ${s.allPassed} | ${s.meanElapsedMs} | ${s.statuses.join(", ")} |`;
        }),
        "", "Environment/model/grader errors remain visible in the denominator. Inspect each trial's result.json, evidence.json and trace.jsonl.",
        "A few repeated trials are a diagnostic sample, not a precise reliability estimate.", ""];
    await writeFile(join(directory, "report.md"), markdown.join("\n"));
    console.log(`Report: ${join(directory, "report.md")}`);
}
main().catch(error => { console.error(error instanceof Error ? error.message : String(error)); process.exitCode = 1; });
