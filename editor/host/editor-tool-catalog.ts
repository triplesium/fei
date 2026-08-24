export interface EditorCommandRequest {
    requestId?: string;
    type?: string;
    path?: string;
    destination?: string;
    content?: string;
    settings?: Record<string, unknown>;
}

export interface EditorToolDefinition {
    command: string;
    name?: string;
    label?: string;
    description?: string;
    inputSchema?: Record<string, unknown>;
    executionMode?: "sequential";
    readOnly?: boolean;
    request?(parameters: unknown): EditorCommandRequest;
}

const emptyInput = Object.freeze({
    type: "object",
    properties: {},
    additionalProperties: false,
});
const pathInput = Object.freeze({
    type: "object",
    properties: {
        path: { type: "string", description: "Project-relative file path" },
    },
    required: ["path"],
    additionalProperties: false,
});

export const editorToolDefinitions: readonly EditorToolDefinition[] = Object.freeze([
    {
        command: "project.list",
        name: "project_list",
        label: "List Project Files",
        description: "List the files in the currently open Entisium project.",
        inputSchema: emptyInput,
        readOnly: true,
        request: () => ({ type: "project.list" }),
    },
    { command: "project.settings.get" },
    { command: "project.settings.update" },
    {
        command: "project.read",
        name: "project_read",
        label: "Read Project File",
        description: "Read a text file from the current Entisium project.",
        inputSchema: pathInput,
        readOnly: true,
        request: (parameters) => {
            const { path } = parameters as { path: string };
            return { type: "project.read", path };
        },
    },
    {
        command: "project.write",
        name: "project_write",
        label: "Write Project File",
        description:
            "Replace a text file in the current Entisium project, or create it if it does not exist.",
        inputSchema: {
            type: "object",
            properties: {
                path: { type: "string", description: "Project-relative file path" },
                content: { type: "string", description: "Complete replacement file contents" },
            },
            required: ["path", "content"],
            additionalProperties: false,
        },
        executionMode: "sequential",
        request: (parameters) => {
            const { path, content } = parameters as { path: string; content: string };
            return { type: "project.write", path, content };
        },
    },
    {
        command: "project.create",
        name: "project_create",
        label: "Create Project File",
        description: "Create a new text file in the current Entisium project.",
        inputSchema: {
            type: "object",
            properties: {
                path: { type: "string", description: "Project-relative file path" },
                content: { type: "string", description: "Initial file contents" },
            },
            required: ["path"],
            additionalProperties: false,
        },
        executionMode: "sequential",
        request: (parameters) => {
            const { path, content } = parameters as { path: string; content?: string };
            return { type: "project.create", path, content };
        },
    },
    { command: "project.rename" },
    { command: "project.remove" },
    {
        command: "runtime.status",
        name: "runtime_status",
        label: "Runtime Status",
        description:
            "Read the current Entisium WebAssembly runtime state, script, and frame status.",
        inputSchema: emptyInput,
        readOnly: true,
        request: () => ({ type: "runtime.status" }),
    },
    {
        command: "runtime.play",
        name: "runtime_play",
        label: "Start Runtime",
        description: "Start the current project in the Entisium WebAssembly runtime.",
        inputSchema: emptyInput,
        executionMode: "sequential",
        request: () => ({ type: "runtime.play" }),
    },
    {
        command: "runtime.stop",
        name: "runtime_stop",
        label: "Stop Runtime",
        description: "Stop the current Entisium WebAssembly runtime session.",
        inputSchema: emptyInput,
        executionMode: "sequential",
        request: () => ({ type: "runtime.stop" }),
    },
    {
        command: "runtime.restart",
        name: "runtime_restart",
        label: "Restart Runtime",
        description: "Restart the current project in the Entisium WebAssembly runtime.",
        inputSchema: emptyInput,
        executionMode: "sequential",
        request: () => ({ type: "runtime.restart" }),
    },
]);
