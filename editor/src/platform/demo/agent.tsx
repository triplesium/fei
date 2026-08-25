import type { AgentEvent } from "@earendil-works/pi-agent-core";
import type {
    EditorModelSettingsSnapshot,
    EditorModelSettingsUpdate,
    EditorSettings,
} from "@/services/editor-host-client";
import type { AgentModelDraft, AgentModelEditorTarget } from "@/components/agent-model-dialog";
import type { AgentRequest, EditorAgentApi, EditorPiAgentApi } from "@/types";

export type { AgentModelDraft, AgentModelEditorTarget, EditorModelSettingsUpdate, EditorSettings };

export type EditorCommandHandler = (request: AgentRequest) => Promise<unknown>;

export type ModelGatewayState =
    | { state: "connecting" }
    | { state: "unavailable"; error: string }
    | {
          state: "unconfigured" | "ready";
          providerId: string;
          provider: string;
          modelId: string;
          model: string;
          settings: EditorModelSettingsSnapshot;
      };

const unavailableState = {
    state: "unavailable",
    error: "The Agent is not included in this Editor build.",
} satisfies ModelGatewayState;

export const editorToolRegistry = {
    createAgentApi(options: {
        handler(command: string): EditorCommandHandler | undefined;
        requestId(): string;
        onInvoke?(command: string): void;
        onError?(message: string): void;
    }): EditorAgentApi {
        return {
            capabilities: [],
            async invoke(request: AgentRequest) {
                const requestId = request.requestId || options.requestId();
                const command = request.type ?? "";
                options.onInvoke?.(command);
                const handler = options.handler(command);
                if (!handler) {
                    const message = `Unsupported Editor command: ${command || "unknown"}`;
                    options.onError?.(message);
                    return { requestId, ok: false, error: { code: "unsupported", message } };
                }
                try {
                    return { requestId, ok: true, value: await handler(request) };
                } catch (error) {
                    const message = error instanceof Error ? error.message : String(error);
                    options.onError?.(message);
                    return { requestId, ok: false, error: { code: "failed", message } };
                }
            },
        };
    },
};

export class EditorPiAgent implements EditorPiAgentApi {
    constructor(_editor: EditorAgentApi) {}

    status() {
        return { configured: false, streaming: false, tools: [] };
    }

    async prompt(): Promise<void> {
        throw new Error(unavailableState.error);
    }

    abort(): void {}
    reset(): void {}
    dispose(): void {}

    subscribe(_listener: (event: AgentEvent) => void | Promise<void>): () => void {
        return () => undefined;
    }

    subscribeState(_listener: () => void): () => void {
        return () => undefined;
    }

    snapshot() {
        return { streaming: false };
    }
}

export class EditorModelGateway {
    connect(): Promise<ModelGatewayState> {
        return Promise.resolve(unavailableState);
    }

    configure(_settings?: unknown, _agent?: unknown): Promise<ModelGatewayState> {
        return Promise.resolve(unavailableState);
    }

    configureProvider(_settings?: unknown, _agent?: unknown): Promise<ModelGatewayState> {
        return Promise.resolve(unavailableState);
    }

    configureRegistryModel(_settings?: unknown, _agent?: unknown): Promise<ModelGatewayState> {
        return Promise.resolve(unavailableState);
    }

    deleteModel(_providerId?: string, _modelId?: string, _agent?: unknown): Promise<ModelGatewayState> {
        return Promise.resolve(unavailableState);
    }

    deleteProvider(_providerId?: string, _agent?: unknown): Promise<ModelGatewayState> {
        return Promise.resolve(unavailableState);
    }

    removeCredential(_providerId?: string, _agent?: unknown): Promise<ModelGatewayState> {
        return Promise.resolve(unavailableState);
    }
}

export function connectEditorCommandBridge(): () => void {
    return () => undefined;
}

export const editorSettings = {
    getEditorSettings(): Promise<EditorSettings> {
        return Promise.resolve({ version: 1, appearance: { agentDensity: "compact" } });
    },
};

export function PiAssistantThread(_props: any) {
    return null;
}

export function AgentModelSelector(_props: any) {
    return null;
}

export function SettingsDialog(_props: any) {
    return null;
}
