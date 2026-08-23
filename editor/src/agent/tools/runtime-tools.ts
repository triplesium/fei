import type { AgentTool } from "@earendil-works/pi-agent-core";
import { Type } from "typebox";
import type { EditorAgentApi } from "../../types";
import { invokeEditorCommand } from "./editor-command";

const emptyParameters = Type.Object({}, { additionalProperties: false });

export function createRuntimeTools(editor: EditorAgentApi): AgentTool<any>[] {
    return [
        {
            name: "runtime_status",
            label: "Runtime Status",
            description: "Read the current Entisium WebAssembly runtime state, script, and frame status.",
            parameters: emptyParameters,
            execute: async (_toolCallId, _parameters, signal) =>
                invokeEditorCommand(editor, { type: "runtime.status" }, signal),
        },
        {
            name: "runtime_play",
            label: "Start Runtime",
            description: "Start the current project in the Entisium WebAssembly runtime.",
            parameters: emptyParameters,
            executionMode: "sequential",
            execute: async (_toolCallId, _parameters, signal) =>
                invokeEditorCommand(editor, { type: "runtime.play" }, signal),
        },
        {
            name: "runtime_stop",
            label: "Stop Runtime",
            description: "Stop the current Entisium WebAssembly runtime session.",
            parameters: emptyParameters,
            executionMode: "sequential",
            execute: async (_toolCallId, _parameters, signal) =>
                invokeEditorCommand(editor, { type: "runtime.stop" }, signal),
        },
        {
            name: "runtime_restart",
            label: "Restart Runtime",
            description: "Restart the current project in the Entisium WebAssembly runtime.",
            parameters: emptyParameters,
            executionMode: "sequential",
            execute: async (_toolCallId, _parameters, signal) =>
                invokeEditorCommand(editor, { type: "runtime.restart" }, signal),
        },
    ];
}
