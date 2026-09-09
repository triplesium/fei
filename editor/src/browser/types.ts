import type { AgentEvent } from "@earendil-works/pi-agent-core";
import type { RuntimeMode } from "./runtime/types";

export type ProjectFileKind = "text" | "binary" | "directory";

export interface ProjectFileEntry {
    path: string;
    kind: ProjectFileKind;
    readonly: boolean;
}

export type ProjectAssetType =
    | "folder"
    | "script"
    | "image"
    | "model"
    | "text"
    | "binary";

export interface ProjectAssetMetadata {
    id: string;
    importer: string;
    settings: Record<string, string>;
    state: "unimported" | "imported";
}

export interface ProjectAssetInspection {
    path: string;
    kind: ProjectFileKind;
    readonly: boolean;
    assetType: ProjectAssetType;
    extension: string;
    size?: number;
    modifiedAt: string;
    mimeType?: string;
    lineCount?: number;
    fileCount?: number;
    directoryCount?: number;
    vertexCount?: number;
    faceCount?: number;
    nodeCount?: number;
    meshCount?: number;
    materialCount?: number;
    metadata?: ProjectAssetMetadata;
    metadataError?: string;
}

export interface RememberedProject {
    name: string;
    restored?: boolean;
    permissionRequired?: boolean;
    source?: "local" | "bundled";
}

export type { RuntimeMode, RuntimeSession, RuntimeState } from "./runtime/types";

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
    afterFrame?: number;
    frame?: number;
    provider?: string;
    schema?: string;
    payload?: unknown;
    mode?: RuntimeMode;
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
