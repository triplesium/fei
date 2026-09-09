import { EditorPiAgent as BaseEditorPiAgent } from "@/agent/editor-pi-agent";
import { createEditorNativeTools } from "@/agent/native-tools";
import type { EditorAgentApi } from "@/types";
export class EditorPiAgent extends BaseEditorPiAgent {
    constructor(editor: EditorAgentApi) { super(editor, createEditorNativeTools()); }
}
export { connectEditorCommandBridge } from "@/agent/editor-command-bridge";
export {
    editorToolRegistry,
    type EditorCommandHandler,
} from "@/agent/editor-tool-registry";
export { EditorModelGateway, type ModelGatewayState } from "@/agent/model-gateway";
export { PiAssistantThread } from "@/agent/pi-assistant-thread";
export {
    type AgentModelDraft,
    type AgentModelEditorTarget,
} from "@/components/agent-model-dialog";
export { AgentModelSelector } from "@/components/agent-model-selector";
export { SettingsDialog } from "@/components/settings-dialog";
export {
    editorHost as editorSettings,
    type EditorModelSettingsUpdate,
    type EditorSettings,
} from "@/services/editor-host-client";
