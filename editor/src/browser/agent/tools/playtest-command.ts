import type { AgentToolUpdateCallback } from "@earendil-works/pi-agent-core";
import type { AgentRequest, EditorAgentApi } from "../../types";
import { abortIfRequested, type EditorCommandDetails } from "./editor-command";

const pollIntervalMs = 250;
const operationTimeoutMs = 120_000;
const commandTimeoutMs = 10_000;
const terminalStates = new Set(["completed", "failed", "stopped", "max_ticks", "cancelled"]);

function deadline<T>(promise: Promise<T>, milliseconds: number): Promise<T> {
    return new Promise((resolve, reject) => {
        const timeout = setTimeout(() => reject(new Error("Playtest wait timed out.")), milliseconds);
        promise.then(resolve, reject).finally(() => clearTimeout(timeout));
    });
}

function wait(signal?: AbortSignal): Promise<void> {
    return new Promise((resolve, reject) => {
        const finish = () => { signal?.removeEventListener("abort", abort); resolve(); };
        const timer = setTimeout(finish, pollIntervalMs);
        const abort = () => {
            clearTimeout(timer);
            signal?.removeEventListener("abort", abort);
            reject(new Error("Playtest cancelled by user."));
        };
        signal?.addEventListener("abort", abort, { once: true });
        if (signal?.aborted) abort();
    });
}

function stateValue(value: unknown): Record<string, unknown> {
    if (!value || typeof value !== "object" || Array.isArray(value)) {
        throw new Error("Invalid playtest response.");
    }
    return value as Record<string, unknown>;
}

export async function invokePlaytestCommand(
    editor: EditorAgentApi,
    request: AgentRequest,
    signal?: AbortSignal,
    onUpdate?: AgentToolUpdateCallback<EditorCommandDetails>,
) {
    abortIfRequested(signal);
    const segment = request.provider === "play.segment";
    const requestId = stateValue(request.payload).request_id;
    if (typeof requestId !== "string") throw new Error("Playtest request ID is missing.");
    const started = Date.now();
    const command = request.provider!;
    let terminal = false;
    let rejected = false;
    const inspect = (provider: string): AgentRequest => ({
        type: "runtime.inspect", provider, schema: `${provider}.v1`,
        payload: { request_id: requestId },
    });
    const invoke = async (next: AgentRequest, timeout = commandTimeoutMs) => {
        const response = await deadline(editor.invoke(next), timeout);
        if (!response.ok) throw new Error(response.error?.message ?? "Playtest command failed.");
        return stateValue(response.value);
    };
    const report = (value: Record<string, unknown>) => {
        const ticks = value.completed_ticks ?? 0;
        const target = value.max_ticks ?? value.target_ticks ?? stateValue(request.payload).max_ticks ?? "?";
        onUpdate?.({
            content: [{ type: "text", text: `Playtest ${String(value.state)}: ${ticks} / ${target} ticks` }],
            details: { command, value },
        });
    };
    try {
        // Await a submitted command even if abort arrives, so its acceptance is not lost.
        const queued = await deadline(editor.invoke(request), commandTimeoutMs);
        if (!queued.ok) {
            rejected = true;
            throw new Error(queued.error?.message ?? "Playtest submission failed.");
        }
        let value = stateValue(queued.value);
        for (;;) {
            if (value.request_id !== requestId) throw new Error("Playtest response request ID mismatch.");
            terminal = terminalStates.has(String(value.state));
            if (terminal) {
                report(value);
                if (value.state === "failed") {
                    throw new Error(`Playtest failed: ${JSON.stringify(value)}`);
                }
                abortIfRequested(signal);
                return {
                    content: [{ type: "text" as const, text: JSON.stringify(value) }],
                    details: { command, value },
                };
            }
            if (value.state !== "pending" && value.state !== "running") {
                throw new Error("Unknown playtest state.");
            }
            abortIfRequested(signal);
            report(value);
            const remaining = operationTimeoutMs - (Date.now() - started);
            if (remaining <= pollIntervalMs) throw new Error("Playtest wait timed out.");
            await wait(signal);
            value = await invoke(inspect(segment ? "play.segment_status" : "play.step_status"),
                Math.min(commandTimeoutMs, remaining - pollIntervalMs));
        }
    } catch (error) {
        if (terminal || rejected) throw error;
        let cleanup = "";
        let cancellationConfirmed = false;
        if (segment) {
            try {
                // Cancellation already consumes the terminal result: do not poll again.
                const cancelled = await invoke(inspect("play.segment_cancel"));
                if (cancelled.request_id !== requestId || !terminalStates.has(String(cancelled.state))) {
                    throw new Error("Segment cancellation did not confirm a terminal state.");
                }
                report(cancelled);
                cancellationConfirmed = true;
                cleanup = `Final segment result: ${JSON.stringify(cancelled)}`;
            } catch {
                cleanup = "Segment cancellation could not be confirmed. ";
            }
        }
        if (!cancellationConfirmed) {
            try {
                const stopped = await deadline(editor.invoke({ type: "runtime.stop" }), commandTimeoutMs);
                if (!stopped.ok) throw new Error(stopped.error?.message ?? "Stop failed.");
                if (stateValue(stopped.value).state !== "stopped") throw new Error("Stop did not confirm the runtime is stopped.");
                cleanup += "Runtime stopped; start it again before continuing.";
            } catch (stopError) {
                cleanup += `Runtime stop could not be confirmed: ${String(stopError)}.`;
            }
        }
        throw new Error(`${error instanceof Error ? error.message : String(error)} Request ${requestId}. Execution may have advanced; do not retry automatically. ${cleanup}`);
    }
}
