import type { AgentTool } from "@earendil-works/pi-agent-core";
import { Type } from "typebox";
import type { EditorAgentApi } from "../types";

interface RuntimeToolDetails {
    command: string;
    value: unknown;
}
const emptyParameters = Type.Object({}, { additionalProperties: false });

function abortIfRequested(signal?: AbortSignal): void {
    if (!signal?.aborted) return;
    throw signal.reason instanceof Error ? signal.reason : new DOMException("Aborted", "AbortError");
}

async function invokeRuntimeCommand(
    editor: EditorAgentApi,
    command: string,
    signal?: AbortSignal,
): Promise<{ content: Array<{ type: "text"; text: string }>; details: RuntimeToolDetails }> {
    abortIfRequested(signal);
    const response = await editor.invoke({ type: command });
    abortIfRequested(signal);
    if (!response.ok) {
        throw new Error(response.error?.message ?? `${command} failed`);
    }
    return {
        content: [{ type: "text", text: JSON.stringify(response.value ?? null) }],
        details: { command, value: response.value },
    };
}

export function createRuntimeTools(editor: EditorAgentApi): AgentTool[] {
    return [
        {
            name: "runtime_status",
            label: "Runtime Status",
            description: "Read the current Fei WebAssembly runtime state, script, and frame status.",
            parameters: emptyParameters,
            execute: async (_toolCallId, _parameters, signal) =>
                invokeRuntimeCommand(editor, "runtime.status", signal),
        },
        {
            name: "runtime_play",
            label: "Start Runtime",
            description: "Start the current project in the Fei WebAssembly runtime.",
            parameters: emptyParameters,
            executionMode: "sequential",
            execute: async (_toolCallId, _parameters, signal) =>
                invokeRuntimeCommand(editor, "runtime.play", signal),
        },
        {
            name: "runtime_stop",
            label: "Stop Runtime",
            description: "Stop the current Fei WebAssembly runtime session.",
            parameters: emptyParameters,
            executionMode: "sequential",
            execute: async (_toolCallId, _parameters, signal) =>
                invokeRuntimeCommand(editor, "runtime.stop", signal),
        },
    ];
}
