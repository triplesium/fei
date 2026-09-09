import { createHash } from "node:crypto";
import { cp, mkdir, writeFile } from "node:fs/promises";
import { join } from "node:path";
import type { AgentEvent } from "@earendil-works/pi-agent-core";
import type { AgentConfiguration } from "@entisium/agent/core/agent";
import { defaultSystemPrompt } from "@entisium/agent/prompts/entisium";
import { runAgentTask } from "@entisium/agent/core/task";
import { createEnvironment, readFiles } from "./environment.js";
import type { Call, Check, EvalCase, TrialResult } from "./types.js";

export async function runTrial(options: {
    task: EvalCase; trial: number; directory: string; configuration?: AgentConfiguration;
    reference?: boolean; timeoutMs: number; maxToolCalls: number; signal?: AbortSignal;
}): Promise<TrialResult> {
    const { task, directory } = options;
    await mkdir(directory, { recursive: false });
    const started = Date.now();
    const result: TrialResult = { caseId: task.id, trial: options.trial, mode: options.reference ? "reference" : "agent",
        status: "environment_error", checks: [], elapsedMs: 0, toolCalls: 0, tokens: null, stopReasons: [], directory };
    const events: unknown[] = [];
    const calls: Call[] = [];
    let answer = "";
    let currentTurn = 0;
    let budgetExceeded = false;
    let verificationAttempted = false;
    let verification: Check[] | undefined;
    let environment: Awaited<ReturnType<typeof createEnvironment>> | undefined;
    const controller = new AbortController();
    const cancel = () => controller.abort(options.signal?.reason);
    options.signal?.addEventListener("abort", cancel, { once: true });
    if (options.signal?.aborted) cancel();
    const timer = setTimeout(() => { budgetExceeded = true; controller.abort(new Error("Trial time budget exceeded.")); }, options.timeoutMs);
    let phase: "environment_error" | "model_error" | "grader_error" = "environment_error";
    try {
        environment = await createEnvironment(task, join(directory, "project"));
        const tools = environment.tools.map(tool => ({ ...tool, execute: async (...args: Parameters<typeof tool.execute>) => {
            controller.signal.throwIfAborted();
            if (calls.length >= options.maxToolCalls) {
                budgetExceeded = true; controller.abort(new Error("Tool-call budget exceeded."));
                throw new Error("Tool-call budget exceeded.");
            }
            const call: Call = { name: tool.name, input: structuredClone(args[1]), turn: currentTurn };
            calls.push(call);
            try {
                const output = await tool.execute(...args);
                call.value = output.details;
                return output;
            } catch (error) { call.error = String(error); throw error; }
        } }));
        const fixtureHash = createHash("sha256").update(JSON.stringify(environment.initial)).digest("hex");
        const turnSnapshots: string[] = [];
        const onTurnEnd = async (turn: number) => {
            const path = join(directory, `turn-${turn + 1}`);
            await cp(join(directory, "project"), path, { recursive: true });
            turnSnapshots.push(path);
            currentTurn = turn + 1;
        };
        await writeFile(join(directory, "manifest.json"), JSON.stringify({
            version: 1, caseId: task.id, taskVersion: task.version ?? 1, tags: task.tags ?? [], environment: task.environment, prompt: task.prompt, fixtureHash,
            followUps: task.followUps ?? [],
            steps: task.steps,
            graderHash: createHash("sha256").update(task.grade.toString()).digest("hex"),
            systemPrompt: defaultSystemPrompt, tools: tools.map(t => ({ name: t.name, description: t.description, parameters: t.parameters })),
            model: options.configuration ? { id: options.configuration.model.id, provider: options.configuration.model.provider,
                api: options.configuration.model.api, reasoning: options.configuration.model.reasoning,
                maxTokens: options.configuration.model.maxTokens, contextWindow: options.configuration.model.contextWindow } : null,
            thinkingLevel: options.configuration?.model.reasoning ? "low" : "off",
            timeoutMs: options.timeoutMs, maxToolCalls: options.maxToolCalls, mode: result.mode,
        }, null, 2));
        const onEvent = (event: AgentEvent) => {
            events.push({ elapsedMs: Date.now() - started, event: structuredClone(event) });
            if (event.type === "message_end" && event.message.role === "assistant") {
                result.stopReasons!.push(event.message.stopReason);
                answer = event.message.content.filter(p => p.type === "text").map(p => p.text).join("\n");
                const usage = event.message.usage;
                if (usage && Number.isFinite(usage.totalTokens)) result.tokens = (result.tokens ?? 0) + usage.totalTokens;
            }
        };
        phase = options.reference ? "environment_error" : "model_error";
        if (options.reference) {
            for (let turn = 0; turn <= (task.followUps?.length ?? 0); turn++) {
                answer = await task.reference(async (name, input) => {
                    const tool = tools.find(t => t.name === name);
                    if (!tool) throw new Error(`Reference requested unavailable tool: ${name}`);
                    return tool.execute(`reference-${calls.length}`, input, controller.signal);
                }, turn);
                await onTurnEnd(turn);
            }
        } else {
            if (!options.configuration) throw new Error("A model configuration is required.");
            await runAgentTask({ prompt: task.prompt, followUps: task.followUps, onTurnEnd, configuration: options.configuration, tools,
                signal: controller.signal, onEvent, cleanup: async () => {} });
        }
        phase = "environment_error";
        await environment.cleanup();
        controller.signal.throwIfAborted();
        // Agent budget does not include trusted verification, which has a separate deadline.
        clearTimeout(timer);
        if (task.verify) {
            verificationAttempted = true;
            const verificationSignal = AbortSignal.any([options.signal ?? new AbortController().signal, AbortSignal.timeout(120_000)]);
            verification = await task.verify(join(directory, "project"), join(directory, "verification"), verificationSignal);
            if (task.followUps?.length) environment.state.turnSnapshots = turnSnapshots;
        }
        const encoded = await readFiles(join(directory, "project"));
        const files = Object.fromEntries(Object.entries(encoded).map(([p, s]) => [p, Buffer.from(s, "base64").toString("utf8")]));
        phase = "grader_error";
        result.checks = task.grade({ files, calls, answer, state: environment.state, verification });
        if (!result.checks.length) throw new Error("No grading checks.");
        if (!task.writable) result.checks.push({ name: "fixture unchanged", passed: JSON.stringify(encoded) === JSON.stringify(environment.initial) });
        result.checks.push({ name: "harness cleanup", passed: environment.state.cleaned === true });
        result.status = result.checks.every(c => c.passed) ? "passed" : "failed";
    } catch (error) {
        result.status = budgetExceeded ? "budget_exceeded" : phase;
        result.error = String(error);
    } finally {
        clearTimeout(timer);
        options.signal?.removeEventListener("abort", cancel);
        try { await environment?.cleanup(); } catch (error) { result.status = "environment_error"; result.error = `Cleanup failed: ${error}`; }
        // Budget/model failures still leave useful candidate artifacts. Score those without
        // converting the failed Agent run into a pass. An explicit user cancellation skips this.
        if (environment && task.verify && !verificationAttempted && !options.signal?.aborted) {
            try {
                const signal = AbortSignal.any([options.signal ?? new AbortController().signal, AbortSignal.timeout(120_000)]);
                verification = await task.verify(join(directory, "project"), join(directory, "verification"), signal);
                const encoded = await readFiles(join(directory, "project"));
                const files = Object.fromEntries(Object.entries(encoded).map(([p, s]) => [p, Buffer.from(s, "base64").toString("utf8")]));
                result.checks = task.grade({ files, calls, answer, state: environment.state, verification });
                result.checks.push({ name: "harness cleanup", passed: environment.state.cleaned === true });
            } catch (error) { environment.state.verificationError = String(error); }
        }
        result.elapsedMs = Date.now() - started;
        result.toolCalls = calls.length;
        if (result.stopReasons?.includes("length") && !["environment_error", "grader_error"].includes(result.status)) {
            result.status = "budget_exceeded";
            result.error = "Model output limit reached (stopReason=length).";
        }
        await writeFile(join(directory, "trace.jsonl"), events.map(e => JSON.stringify(e)).join("\n") + "\n");
        await writeFile(join(directory, "evidence.json"), JSON.stringify({ calls, answer, state: environment?.state, verification }, null, 2));
        await writeFile(join(directory, "result.json"), JSON.stringify(result, null, 2));
    }
    return result;
}

export function summarize(results: TrialResult[]) {
    return [...new Set(results.map(r => r.caseId))].map(caseId => {
        const trials = results.filter(r => r.caseId === caseId);
        const passed = trials.filter(r => r.status === "passed").length;
        return { caseId, passed, trials: trials.length, allPassed: passed === trials.length,
            dimensions: Object.fromEntries(["outcome", "verification", "constraints"].map(dimension => {
                const measured = trials.map(t => t.checks.filter(c => c.dimension === dimension)).filter(c => c.length);
                return [dimension, { passed: measured.filter(checks => checks.every(c => c.passed)).length, total: measured.length }];
            })),
            meanElapsedMs: Math.round(trials.reduce((s, r) => s + r.elapsedMs, 0) / trials.length),
            statuses: trials.map(r => r.status) };
    });
}
