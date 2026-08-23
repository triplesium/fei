import type { AgentToolResult } from "@earendil-works/pi-agent-core";
import type { AgentRequest, EditorAgentApi } from "../../types";

export interface EditorCommandDetails {
    command: string;
    value: unknown;
}

export function abortIfRequested(signal?: AbortSignal): void {
    if (!signal?.aborted) return;
    throw signal.reason instanceof Error
        ? signal.reason
        : new DOMException("Aborted", "AbortError");
}

export async function invokeEditorCommand(
    editor: EditorAgentApi,
    request: AgentRequest,
    signal?: AbortSignal,
): Promise<AgentToolResult<EditorCommandDetails>> {
    const command = request.type ?? "unknown";
    abortIfRequested(signal);
    const response = await editor.invoke(request);
    abortIfRequested(signal);
    if (!response.ok) {
        throw new Error(response.error?.message ?? `${command} failed`);
    }
    return {
        content: [{ type: "text", text: JSON.stringify(response.value ?? null) }],
        details: { command, value: response.value },
    };
}
