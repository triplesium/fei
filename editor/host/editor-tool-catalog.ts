export interface EditorCommandRequest {
    requestId?: string;
    type?: string;
    path?: string;
    destination?: string;
    content?: string;
    settings?: Record<string, unknown>;
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

function playStepRequestId(): string {
    return globalThis.crypto?.randomUUID?.() ?? `play-${Date.now()}-${Math.random()}`;
}

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
        command: "runtime.observe",
        name: "runtime_observe",
        label: "Observe Runtime",
        description:
            "Capture the current game viewport as an image. Use this before and after input to understand the game state.",
        inputSchema: emptyInput,
        readOnly: true,
        request: () => ({ type: "runtime.observe" }),
    },
    {
        command: "runtime.key",
        name: "runtime_key",
        label: "Send Keyboard Input",
        description:
            "Press, release, or tap a keyboard key in the running game. Use DOM key codes such as KeyW, ArrowLeft, Space, or Enter.",
        inputSchema: {
            type: "object",
            properties: {
                code: {
                    type: "string",
                    description: "DOM KeyboardEvent code, for example KeyW, ArrowUp, Space, or Enter",
                },
                action: {
                    type: "string",
                    enum: ["press", "release", "tap"],
                    description: "Whether to hold, release, or briefly tap the key",
                },
                durationMs: {
                    type: "number",
                    minimum: 0,
                    maximum: 5000,
                    description: "Tap duration in milliseconds; defaults to 80",
                },
            },
            required: ["code", "action"],
            additionalProperties: false,
        },
        executionMode: "sequential",
        request: (parameters) => {
            const { code, action, durationMs } = parameters as {
                code: string;
                action: string;
                durationMs?: number;
            };
            return { type: "runtime.key", code, action, durationMs };
        },
    },
    {
        command: "runtime.pointer",
        name: "runtime_pointer",
        label: "Send Pointer Input",
        description:
            "Move, press, release, or click the game pointer at normalized viewport coordinates from 0 to 1.",
        inputSchema: {
            type: "object",
            properties: {
                x: { type: "number", minimum: 0, maximum: 1 },
                y: { type: "number", minimum: 0, maximum: 1 },
                action: { type: "string", enum: ["move", "press", "release", "click"] },
                button: {
                    type: "string",
                    enum: ["left", "middle", "right"],
                    description: "Defaults to left",
                },
                durationMs: {
                    type: "number",
                    minimum: 0,
                    maximum: 5000,
                    description: "Click duration in milliseconds; defaults to 50",
                },
            },
            required: ["x", "y", "action"],
            additionalProperties: false,
        },
        executionMode: "sequential",
        request: (parameters) => {
            const { x, y, action, button, durationMs } = parameters as {
                x: number;
                y: number;
                action: string;
                button?: string;
                durationMs?: number;
            };
            return { type: "runtime.pointer", x, y, action, button, durationMs };
        },
    },
    {
        command: "runtime.wait",
        name: "runtime_wait",
        label: "Wait for Runtime",
        description: "Wait briefly for the game to advance, then return its runtime status.",
        inputSchema: {
            type: "object",
            properties: {
                durationMs: {
                    type: "number",
                    minimum: 0,
                    maximum: 5000,
                    description: "Duration in milliseconds",
                },
            },
            required: ["durationMs"],
            additionalProperties: false,
        },
        readOnly: true,
        executionMode: "sequential",
        request: (parameters) => {
            const { durationMs } = parameters as { durationMs: number };
            return { type: "runtime.wait", durationMs };
        },
    },
    {
        command: "runtime.clear_input",
        name: "runtime_clear_input",
        label: "Clear Runtime Input",
        description: "Release all keyboard keys and pointer buttons currently held by the agent.",
        inputSchema: emptyInput,
        executionMode: "sequential",
        request: () => ({ type: "runtime.clear_input" }),
    },
    {
        command: "runtime.logs",
        name: "runtime_logs",
        label: "Read Runtime Logs",
        description: "Read recent game and runtime console messages.",
        inputSchema: {
            type: "object",
            properties: {
                limit: {
                    type: "number",
                    minimum: 1,
                    maximum: 100,
                    description: "Maximum messages to return; defaults to 20",
                },
            },
            additionalProperties: false,
        },
        readOnly: true,
        request: (parameters) => {
            const { limit } = parameters as { limit?: number };
            return { type: "runtime.logs", limit };
        },
    },
    {
        command: "profiler.summary",
        name: "profiler_summary",
        label: "Read Profiler Summary",
        description:
            "Read capture-wide CPU and GPU statistics and hotspots. Use this first during performance investigations. Reads the retained capture after the runtime stops.",
        inputSchema: emptyInput,
        readOnly: true,
        request: () => ({ type: "profiler.summary" }),
    },
    {
        command: "profiler.frames",
        name: "profiler_frames",
        label: "Read Profiler Frames",
        description:
            "Read recent frame times to locate spikes and periodic patterns. Reads the retained capture after the runtime stops.",
        inputSchema: {
            type: "object",
            properties: {
                afterFrame: {
                    type: "integer",
                    minimum: 0,
                    description: "Return only frames newer than this frame number",
                },
                limit: {
                    type: "integer",
                    minimum: 1,
                    maximum: 600,
                    description: "Maximum frames to return; defaults to 300",
                },
            },
            additionalProperties: false,
        },
        readOnly: true,
        request: (parameters) => {
            const { afterFrame, limit } = parameters as {
                afterFrame?: number;
                limit?: number;
            };
            return { type: "profiler.frames", afterFrame, limit };
        },
    },
    {
        command: "profiler.frame",
        name: "profiler_frame",
        label: "Inspect Profiler Frame",
        description:
            "Inspect CPU systems and zones for one frame selected from profiler_frames, including symbolized function and source information.",
        inputSchema: {
            type: "object",
            properties: {
                frame: {
                    type: "integer",
                    minimum: 0,
                    description: "Frame number returned by profiler_frames",
                },
            },
            required: ["frame"],
            additionalProperties: false,
        },
        readOnly: true,
        request: (parameters) => {
            const { frame } = parameters as { frame: number };
            return { type: "profiler.frame", frame };
        },
    },
    {
        command: "runtime.inspect",
        name: "play_interfaces",
        label: "List Play Interfaces",
        description:
            "List the structured actions, observations, and deterministic tick rules exposed by the running game.",
        inputSchema: emptyInput,
        readOnly: true,
        request: () => ({
            type: "runtime.inspect",
            provider: "play.interfaces",
            schema: "play.interfaces.v1",
            payload: {},
        }),
    },
    {
        command: "runtime.inspect",
        name: "play_observe",
        label: "Observe Structured Play State",
        description:
            "Read the current structured observation from a play interface. Prefer this over screenshots when an interface is available.",
        inputSchema: {
            type: "object",
            properties: {
                interface: {
                    type: "string",
                    description: "Interface id returned by play_interfaces",
                },
            },
            required: ["interface"],
            additionalProperties: false,
        },
        readOnly: true,
        request: (parameters) => {
            const { interface: interfaceId } = parameters as { interface: string };
            return {
                type: "runtime.inspect",
                provider: "play.observe",
                schema: "play.observe.v1",
                payload: { interface: interfaceId },
            };
        },
    },
    {
        command: "runtime.inspect",
        name: "play_step",
        label: "Queue Structured Play Step",
        description:
            "Queue a schema-validated action for deterministic execution by the running game. Poll the returned request_id with play_step_status.",
        inputSchema: {
            type: "object",
            properties: {
                interface: {
                    type: "string",
                    description: "Interface id returned by play_interfaces",
                },
                action: {
                    description: "Action matching the interface action_schema",
                },
                ticks: {
                    type: "integer",
                    minimum: 1,
                    description: "Optional fixed-tick override allowed by the interface",
                },
            },
            required: ["interface", "action"],
            additionalProperties: false,
        },
        executionMode: "sequential",
        request: (parameters) => {
            const { interface: interfaceId, action, ticks } = parameters as {
                interface: string;
                action: unknown;
                ticks?: number;
            };
            return {
                type: "runtime.inspect",
                provider: "play.step",
                schema: "play.step.v1",
                payload: {
                    request_id: playStepRequestId(),
                    interface: interfaceId,
                    action,
                    ...(ticks === undefined ? {} : { ticks }),
                },
            };
        },
    },
    {
        command: "runtime.inspect",
        name: "play_step_status",
        label: "Poll Structured Play Step",
        description:
            "Poll a queued play step by request_id. A completed or failed result is consumed when returned.",
        inputSchema: {
            type: "object",
            properties: {
                requestId: {
                    type: "string",
                    description: "request_id returned by play_step",
                },
            },
            required: ["requestId"],
            additionalProperties: false,
        },
        executionMode: "sequential",
        request: (parameters) => {
            const { requestId } = parameters as { requestId: string };
            return {
                type: "runtime.inspect",
                provider: "play.step_status",
                schema: "play.step_status.v1",
                payload: { request_id: requestId },
            };
        },
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
