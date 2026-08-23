import type { AgentEvent } from "@earendil-works/pi-agent-core";

export type ProjectFileKind = "text" | "binary";

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

declare global {
    interface Window {
        entisiumEditorAgent: EditorAgentApi;
        entisiumEditorPi: EditorPiAgentApi;
    }
}
