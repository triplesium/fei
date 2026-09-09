import type { AgentTool } from "@earendil-works/pi-agent-core";
import { EntisiumAgent } from "@entisium/agent/core/agent";
import type { EditorAgentApi } from "../types";
import { createEditorTools } from "./tools/index";
export type {
    AgentConfiguration as EditorPiAgentConfiguration,
    AgentSnapshot as EditorPiAgentSnapshot,
} from "@entisium/agent/core/agent";

/** UI adapter; conversation and tool execution live in the shared agent core. */
export class EditorPiAgent extends EntisiumAgent {
    constructor(editor: EditorAgentApi, additionalTools: AgentTool<any>[] = []) {
        super([...createEditorTools(editor), ...additionalTools]);
    }
}
