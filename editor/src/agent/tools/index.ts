import type { AgentTool } from "@earendil-works/pi-agent-core";
import type { EditorAgentApi } from "../../types";
import { createProjectTools } from "./project-tools";
import { createRuntimeTools } from "./runtime-tools";

export function createEditorTools(editor: EditorAgentApi): AgentTool<any>[] {
    return [...createProjectTools(editor), ...createRuntimeTools(editor)];
}
