import type { AgentTool } from "@earendil-works/pi-agent-core";
import { Type } from "typebox";
import type { EditorAgentApi } from "../../types";
import { invokeEditorCommand } from "./editor-command";

const emptyParameters = Type.Object({}, { additionalProperties: false });
const pathParameters = Type.Object(
    {
        path: Type.String({ description: "Project-relative file path" }),
    },
    { additionalProperties: false },
);

export function createProjectTools(editor: EditorAgentApi): AgentTool<any>[] {
    return [
        {
            name: "project_list",
            label: "List Project Files",
            description: "List the files in the currently open Fei project.",
            parameters: emptyParameters,
            execute: async (_toolCallId, _parameters, signal) =>
                invokeEditorCommand(editor, { type: "project.list" }, signal),
        },
        {
            name: "project_read",
            label: "Read Project File",
            description: "Read a text file from the current Fei project.",
            parameters: pathParameters,
            execute: async (_toolCallId, parameters, signal) => {
                const { path } = parameters as { path: string };
                return invokeEditorCommand(editor, { type: "project.read", path }, signal);
            },
        },
        {
            name: "project_write",
            label: "Write Project File",
            description:
                "Replace a text file in the current Fei project, or create it if it does not exist.",
            parameters: Type.Object(
                {
                    path: Type.String({ description: "Project-relative file path" }),
                    content: Type.String({ description: "Complete replacement file contents" }),
                },
                { additionalProperties: false },
            ),
            executionMode: "sequential",
            execute: async (_toolCallId, parameters, signal) => {
                const { path, content } = parameters as {
                    path: string;
                    content: string;
                };
                return invokeEditorCommand(
                    editor,
                    { type: "project.write", path, content },
                    signal,
                );
            },
        },
        {
            name: "project_create",
            label: "Create Project File",
            description: "Create a new text file in the current Fei project.",
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
            execute: async (_toolCallId, parameters, signal) => {
                const { path, content } = parameters as {
                    path: string;
                    content?: string;
                };
                return invokeEditorCommand(
                    editor,
                    { type: "project.create", path, content },
                    signal,
                );
            },
        },
    ];
}
