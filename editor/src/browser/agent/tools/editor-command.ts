import type { AgentToolResult } from "@earendil-works/pi-agent-core";
import type { AgentRequest, EditorAgentApi } from "../../types";

export interface EditorCommandDetails {
    command: string;
    value: unknown;
}

function asImage(value: unknown):
    | { mimeType: string; data: string; width?: number; height?: number }
    | undefined {
    if (!value || typeof value !== "object" || Array.isArray(value)) return undefined;
    const image = value as Record<string, unknown>;
    if (typeof image.mimeType !== "string" || typeof image.data !== "string") return undefined;
    return {
        mimeType: image.mimeType,
        data: image.data,
        width: typeof image.width === "number" ? image.width : undefined,
        height: typeof image.height === "number" ? image.height : undefined,
    };
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
    const image = asImage(response.value);
    return {
        content: image
            ? [
                  {
                      type: "text",
                      text: `Runtime viewport${image.width && image.height ? ` (${image.width}x${image.height})` : ""}.`,
                  },
                  { type: "image", data: image.data, mimeType: image.mimeType },
              ]
            : [{ type: "text", text: JSON.stringify(response.value ?? null) }],
        details: { command, value: response.value },
    };
}
