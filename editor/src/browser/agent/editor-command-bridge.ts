import { editorHost } from "../services/editor-host-client";
import type { AgentRequest, EditorAgentApi } from "../types";

interface EditorCommandEvent {
    type: "command";
    id: string;
    request: AgentRequest;
}

function asCommandEvent(value: unknown): EditorCommandEvent | undefined {
    if (!value || typeof value !== "object" || Array.isArray(value)) return undefined;
    const event = value as Partial<EditorCommandEvent>;
    if (
        event.type !== "command" ||
        typeof event.id !== "string" ||
        !event.request ||
        typeof event.request !== "object" ||
        Array.isArray(event.request)
    ) {
        return undefined;
    }
    return event as EditorCommandEvent;
}

export function connectEditorCommandBridge(editor: EditorAgentApi): () => void {
    return editorHost.subscribeEvents("/api/v1/editor/commands", (value) => {
        const event = asCommandEvent(value);
        if (!event) return;
        void editor
            .invoke(event.request)
            .then((response) =>
                editorHost.request("/api/v1/editor/commands/result", {
                    method: "POST",
                    headers: { "Content-Type": "application/json" },
                    body: JSON.stringify({ id: event.id, response }),
                }),
            )
            .catch((error) => {
                console.error("[entisium editor] failed to complete external command", error);
            });
    });
}
