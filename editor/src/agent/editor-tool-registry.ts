import type { AgentTool } from "@earendil-works/pi-agent-core";
import { Type, type TSchema } from "typebox";
import type {
    AgentRequest,
    AgentResponse,
    EditorAgentApi,
} from "../types";
import { invokeEditorCommand } from "./tools/editor-command";

export type EditorCommandHandler = (request: AgentRequest) => Promise<unknown>;

interface EditorPiToolDefinition {
    name: string;
    label: string;
    description: string;
    parameters: TSchema;
    executionMode?: "sequential";
    request(parameters: unknown): AgentRequest;
}

interface EditorCommandDefinition {
    command: string;
    piTool?: EditorPiToolDefinition;
}

export interface EditorAgentApiOptions {
    handler(command: string): EditorCommandHandler | undefined;
    requestId(): string;
    onInvoke?(command: string): void;
    onError?(message: string): void;
}

const emptyParameters = Type.Object({}, { additionalProperties: false });
const pathParameters = Type.Object(
    {
        path: Type.String({ description: "Project-relative file path" }),
    },
    { additionalProperties: false },
);

const commandDefinitions: readonly EditorCommandDefinition[] = Object.freeze([
    {
        command: "project.list",
        piTool: {
            name: "project_list",
            label: "List Project Files",
            description: "List the files in the currently open Entisium project.",
            parameters: emptyParameters,
            request: () => ({ type: "project.list" }),
        },
    },
    { command: "project.settings.get" },
    { command: "project.settings.update" },
    {
        command: "project.read",
        piTool: {
            name: "project_read",
            label: "Read Project File",
            description: "Read a text file from the current Entisium project.",
            parameters: pathParameters,
            request: (parameters: unknown) => {
                const { path } = parameters as { path: string };
                return { type: "project.read", path };
            },
        },
    },
    {
        command: "project.write",
        piTool: {
            name: "project_write",
            label: "Write Project File",
            description:
                "Replace a text file in the current Entisium project, or create it if it does not exist.",
            parameters: Type.Object(
                {
                    path: Type.String({ description: "Project-relative file path" }),
                    content: Type.String({ description: "Complete replacement file contents" }),
                },
                { additionalProperties: false },
            ),
            executionMode: "sequential",
            request: (parameters: unknown) => {
                const { path, content } = parameters as { path: string; content: string };
                return { type: "project.write", path, content };
            },
        },
    },
    {
        command: "project.create",
        piTool: {
            name: "project_create",
            label: "Create Project File",
            description: "Create a new text file in the current Entisium project.",
            parameters: Type.Object(
                {
                    path: Type.String({ description: "Project-relative file path" }),
                    content: Type.Optional(
                        Type.String({ description: "Initial file contents" }),
                    ),
                },
                { additionalProperties: false },
            ),
            executionMode: "sequential",
            request: (parameters: unknown) => {
                const { path, content } = parameters as { path: string; content?: string };
                return { type: "project.create", path, content };
            },
        },
    },
    { command: "project.rename" },
    { command: "project.remove" },
    {
        command: "runtime.status",
        piTool: {
            name: "runtime_status",
            label: "Runtime Status",
            description:
                "Read the current Entisium WebAssembly runtime state, script, and frame status.",
            parameters: emptyParameters,
            request: () => ({ type: "runtime.status" }),
        },
    },
    {
        command: "runtime.play",
        piTool: {
            name: "runtime_play",
            label: "Start Runtime",
            description: "Start the current project in the Entisium WebAssembly runtime.",
            parameters: emptyParameters,
            executionMode: "sequential",
            request: () => ({ type: "runtime.play" }),
        },
    },
    {
        command: "runtime.stop",
        piTool: {
            name: "runtime_stop",
            label: "Stop Runtime",
            description: "Stop the current Entisium WebAssembly runtime session.",
            parameters: emptyParameters,
            executionMode: "sequential",
            request: () => ({ type: "runtime.stop" }),
        },
    },
    {
        command: "runtime.restart",
        piTool: {
            name: "runtime_restart",
            label: "Restart Runtime",
            description: "Restart the current project in the Entisium WebAssembly runtime.",
            parameters: emptyParameters,
            executionMode: "sequential",
            request: () => ({ type: "runtime.restart" }),
        },
    },
]);

function errorMessage(error: unknown): string {
    return error instanceof Error ? error.message : String(error);
}

export class EditorToolRegistry {
    readonly capabilities = Object.freeze(
        commandDefinitions.map((definition) => definition.command),
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
        return commandDefinitions.flatMap((definition) => {
            const tool = definition.piTool;
            if (!tool) return [];
            return [
                {
                    name: tool.name,
                    label: tool.label,
                    description: tool.description,
                    parameters: tool.parameters,
                    executionMode: tool.executionMode,
                    execute: async (_toolCallId, parameters, signal) =>
                        invokeEditorCommand(editor, tool.request(parameters), signal),
                },
            ];
        });
    }
}

export const editorToolRegistry = new EditorToolRegistry();
