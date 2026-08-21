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

export type RuntimeState = "stopped" | "starting" | "running" | "failed";

export interface RuntimeSession {
    channelId: string;
    files: Array<{ path: string; content: string | Uint8Array<ArrayBuffer> }>;
    source: string;
}

export type ConsoleLevel = "info" | "error" | "command";

export interface ConsoleEntry {
    id: number;
    level: ConsoleLevel;
    source: string;
    message: string;
    time: string;
}

export interface AgentRequest {
    requestId?: string;
    type?: string;
    path?: string;
    destination?: string;
    content?: string;
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

declare global {
    interface Window {
        feiEditorAgent: EditorAgentApi;
    }
}
