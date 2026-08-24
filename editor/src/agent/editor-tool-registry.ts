import type { AgentTool } from "@earendil-works/pi-agent-core";
import type { TSchema } from "typebox";
import { editorToolDefinitions } from "../../host/editor-tool-catalog";
import type {
    AgentRequest,
    AgentResponse,
    EditorAgentApi,
} from "../types";
import { invokeEditorCommand } from "./tools/editor-command";

export type EditorCommandHandler = (request: AgentRequest) => Promise<unknown>;

export interface EditorAgentApiOptions {
    handler(command: string): EditorCommandHandler | undefined;
    requestId(): string;
    onInvoke?(command: string): void;
    onError?(message: string): void;
}

function errorMessage(error: unknown): string {
    return error instanceof Error ? error.message : String(error);
}

export class EditorToolRegistry {
    readonly capabilities = Object.freeze(
        [...new Set(editorToolDefinitions.map((definition) => definition.command))],
    );

    createAgentApi(options: EditorAgentApiOptions): EditorAgentApi {
        return {
            capabilities: this.capabilities,
            invoke: async (request: AgentRequest): Promise<AgentResponse> => {
                const id = request?.requestId ?? options.requestId();
                const command = request?.type ?? "";
                options.onInvoke?.(command);
                try {
                    const handler = options.handler(command);
                    if (!handler) throw new Error(`Unsupported editor command: ${command}`);
                    return { requestId: id, ok: true, value: await handler(request) };
                } catch (error) {
                    const message = errorMessage(error);
                    options.onError?.(message);
                    return {
                        requestId: id,
                        ok: false,
                        error: { code: "command_failed", message },
                    };
                }
            },
        };
    }

    createPiTools(editor: EditorAgentApi): AgentTool<any>[] {
        return editorToolDefinitions.flatMap((definition) => {
            if (
                !definition.name ||
                !definition.label ||
                !definition.description ||
                !definition.inputSchema ||
                !definition.request
            ) {
                return [];
            }
            return [
                {
                    name: definition.name,
                    label: definition.label,
                    description: definition.description,
                    parameters: definition.inputSchema as TSchema,
                    executionMode: definition.executionMode,
                    execute: async (_toolCallId, parameters, signal) =>
                        invokeEditorCommand(editor, definition.request!(parameters), signal),
                },
            ];
        });
    }
}

export const editorToolRegistry = new EditorToolRegistry();
