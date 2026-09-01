import {
    Agent,
    type AgentEvent,
    type AgentMessage,
    type StreamFn,
} from "@earendil-works/pi-agent-core";
import type { Api, Model } from "@earendil-works/pi-ai";
import type { EditorAgentApi, EditorPiAgentApi, EditorPiAgentStatus } from "../types";
import { createEditorTools } from "./tools";

const systemPrompt = `You are the built-in agent for the Entisium Editor.
Use the available tools to inspect and edit the current project and control its WebAssembly runtime.
When playing a game, start it with runtime_play in playtest mode, then call play_interfaces. Prefer structured play_observe and play_step calls, polling play_step_status until completed. Fall back to viewport capture and input tools when the game exposes no structured interface. Release held inputs when they are no longer needed.
For performance investigations, call profiler_summary first, use profiler_frames to locate spikes, and inspect only relevant frames with profiler_frame. These tools can read the retained capture after the runtime stops.
Do not claim an operation succeeded until its tool result confirms success.`;

const unconfiguredStream: StreamFn = () => {
    throw new Error("The Editor model gateway is not configured.");
};

export interface EditorPiAgentConfiguration {
    model: Model<Api>;
    streamFn: StreamFn;
}

export interface EditorPiAgentSnapshot {
    messages: readonly AgentMessage[];
    streamingMessage?: AgentMessage;
    streaming: boolean;
    pendingToolCalls: ReadonlySet<string>;
    error?: string;
    configured: boolean;
}

export class EditorPiAgent implements EditorPiAgentApi {
    private readonly agent: Agent;
    private configured = false;
    private readonly toolNames: readonly string[];
    private readonly stateListeners = new Set<() => void>();
    private readonly unsubscribeStateEvents: () => void;

    constructor(editor: EditorAgentApi) {
        const tools = createEditorTools(editor);
        this.toolNames = Object.freeze(tools.map((tool) => tool.name));
        this.agent = new Agent({
            initialState: {
                systemPrompt,
                thinkingLevel: "off",
                tools,
            },
            streamFn: unconfiguredStream,
            toolExecution: "sequential",
        });
        this.unsubscribeStateEvents = this.agent.subscribe(() => this.emitState());
    }

    configure(configuration: EditorPiAgentConfiguration): void {
        if (this.agent.state.isStreaming) {
            throw new Error("Cannot change the model while the agent is running.");
        }
        this.agent.state.model = configuration.model;
        this.agent.state.thinkingLevel = configuration.model.reasoning ? "low" : "off";
        this.agent.streamFunction = configuration.streamFn;
        this.configured = true;
        this.emitState();
    }

    unconfigure(): void {
        if (this.agent.state.isStreaming) this.agent.abort();
        this.agent.streamFunction = unconfiguredStream;
        this.configured = false;
        this.emitState();
    }

    status(): EditorPiAgentStatus {
        return {
            configured: this.configured,
            streaming: this.agent.state.isStreaming,
            tools: this.toolNames,
            error: this.agent.state.errorMessage,
        };
    }

    async prompt(input: string): Promise<void> {
        if (!this.configured) throw new Error("The Editor model gateway is not configured.");
        const prompt = input.trim();
        if (!prompt) throw new Error("Agent prompt cannot be empty.");
        try {
            await this.agent.prompt(prompt);
        } finally {
            // Pi clears isStreaming after awaited agent_end listeners have settled.
            this.emitState();
        }
    }

    abort(): void {
        this.agent.abort();
    }

    reset(): void {
        this.agent.reset();
        this.emitState();
    }

    subscribe(listener: (event: AgentEvent) => void | Promise<void>): () => void {
        return this.agent.subscribe((event) => listener(event));
    }

    snapshot(): EditorPiAgentSnapshot {
        return {
            messages: this.agent.state.messages,
            streamingMessage: this.agent.state.streamingMessage,
            streaming: this.agent.state.isStreaming,
            pendingToolCalls: this.agent.state.pendingToolCalls,
            error: this.agent.state.errorMessage,
            configured: this.configured,
        };
    }

    subscribeState(listener: () => void): () => void {
        this.stateListeners.add(listener);
        return () => this.stateListeners.delete(listener);
    }

    dispose(): void {
        this.agent.abort();
        this.unsubscribeStateEvents();
        this.stateListeners.clear();
    }

    private emitState(): void {
        for (const listener of this.stateListeners) listener();
    }
}
