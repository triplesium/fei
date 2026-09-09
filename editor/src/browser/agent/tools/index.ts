import type { AgentTool } from "@earendil-works/pi-agent-core";
import type { EditorAgentApi } from "../../types";
import { editorToolRegistry } from "../editor-tool-registry";

export function createEditorTools(editor: EditorAgentApi): AgentTool<any>[] {
    return editorToolRegistry.createPiTools(editor);
}
