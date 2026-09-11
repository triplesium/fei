import { EditorPiAgent as BaseEditorPiAgent } from "@/agent/editor-pi-agent";
import { createEditorNativeTools } from "@/agent/native-tools";
import type { EditorAgentApi } from "@/types";
import { createImageGenerationTools } from "@entisium/agent/tools/image-generation";
import { createSpriteAnimationTools } from "@entisium/agent/tools/sprite-animation";
import { editorHost } from "@/services/editor-host-client";
export class EditorPiAgent extends BaseEditorPiAgent {
    constructor(editor: EditorAgentApi) {
        super(editor, [...createEditorNativeTools(), ...createImageGenerationTools((input, signal) =>
            editorHost.json("/api/v1/image-generation", {
                method: "POST", signal, headers: { "Content-Type": "application/json" }, body: JSON.stringify(input),
            })), ...createSpriteAnimationTools((input, signal) =>
            editorHost.json("/api/v1/sprite-animation", {
                method: "POST", signal, headers: { "Content-Type": "application/json" }, body: JSON.stringify(input),
            }))]);
    }
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
