import type { AgentEvent } from "@earendil-works/pi-agent-core";

export type ProjectFileKind = "text" | "binary" | "directory";

export interface ProjectFileEntry {
    path: string;
    kind: ProjectFileKind;
    readonly: boolean;
}

export interface RememberedProject {
    name: string;
    restored?: boolean;
    permissionRequired?: boolean;
}

export type { RuntimeSession, RuntimeState } from "./runtime/types";

export type ConsoleLevel = "info" | "error" | "command";

export interface ConsoleEntry {
    id: string;
    level: ConsoleLevel;
    source: string;
    message: string;
    time: string;
}

export interface ProjectSettings {
    name: string;
    assetDirectory: string;
    runtimePlugins: string[];
}

export interface AgentRequest {
    requestId?: string;
    type?: string;
    path?: string;
    destination?: string;
    content?: string;
    settings?: Partial<ProjectSettings>;
    code?: string;
    action?: string;
    durationMs?: number;
    x?: number;
    y?: number;
    button?: string;
    limit?: number;
    provider?: string;
    schema?: string;
    payload?: unknown;
}

export interface AgentResponse {
    requestId: string;
    ok: boolean;
    value?: unknown;
    error?: { code: string; message: string };
}

export interface EditorAgentApi {
    capabilities: readonly string[];
    invoke(request: AgentRequest): Promise<AgentResponse>;
}

export interface EditorPiAgentStatus {
    configured: boolean;
    streaming: boolean;
    tools: readonly string[];
    error?: string;
}

export interface EditorPiAgentApi {
    status(): EditorPiAgentStatus;
    prompt(input: string): Promise<void>;
    abort(): void;
    reset(): void;
    subscribe(listener: (event: AgentEvent) => void | Promise<void>): () => void;
}

export interface EntisiumEditorApi {
    commands: EditorAgentApi;
    agents: {
        pi: EditorPiAgentApi;
    };
}

declare global {
    interface Window {
        entisiumEditor: EntisiumEditorApi;
        /** @deprecated Use entisiumEditor.commands. */
        entisiumEditorAgent: EditorAgentApi;
        /** @deprecated Use entisiumEditor.agents.pi. */
        entisiumEditorPi: EditorPiAgentApi;
    }
}
