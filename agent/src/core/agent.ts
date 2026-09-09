import {
    Agent,
    type AgentTool,
    type AgentEvent,
    type AgentMessage,
    type StreamFn,
} from "@earendil-works/pi-agent-core";
import type { Api, Model } from "@earendil-works/pi-ai";

import { defaultSystemPrompt } from "../prompts/entisium.js";
export { defaultSystemPrompt } from "../prompts/entisium.js";

const unconfiguredStream: StreamFn = () => {
    throw new Error("The agent model is not configured.");
};

export interface AgentConfiguration {
    model: Model<Api>;
    streamFn: StreamFn;
}

export interface AgentSnapshot {
    messages: readonly AgentMessage[];
    streamingMessage?: AgentMessage;
    streaming: boolean;
    pendingToolCalls: ReadonlySet<string>;
    toolProgress?: ReadonlyMap<string, string>;
    error?: string;
    configured: boolean;
}

export class EntisiumAgent {
    private readonly agent: Agent;
    private configured = false;
    private readonly toolNames: readonly string[];
    private readonly stateListeners = new Set<() => void>();
    private readonly toolProgress = new Map<string, string>();
    private readonly unsubscribeStateEvents: () => void;

    constructor(tools: AgentTool<any>[], systemPrompt = defaultSystemPrompt) {
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
        this.unsubscribeStateEvents = this.agent.subscribe((event) => {
            if (event.type === "tool_execution_update") {
                const parts = event.partialResult?.content;
                if (Array.isArray(parts)) {
                    this.toolProgress.set(event.toolCallId, parts.filter((part) => part.type === "text").map((part) => part.text).join("\n"));
                }
            } else if (event.type === "tool_execution_end") {
                this.toolProgress.delete(event.toolCallId);
            } else if (event.type === "agent_end") {
                this.toolProgress.clear();
            }
            this.emitState();
        });
    }

    configure(configuration: AgentConfiguration): void {
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

    status() {
        return {
            configured: this.configured,
            streaming: this.agent.state.isStreaming,
            tools: this.toolNames,
            error: this.agent.state.errorMessage,
        };
    }

    async prompt(input: string): Promise<void> {
        if (!this.configured) throw new Error("The agent model is not configured.");
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
        this.toolProgress.clear();
        this.agent.reset();
        this.emitState();
    }

    subscribe(listener: (event: AgentEvent) => void | Promise<void>): () => void {
        return this.agent.subscribe((event) => listener(event));
    }

    snapshot(): AgentSnapshot {
        return {
            messages: this.agent.state.messages,
            streamingMessage: this.agent.state.streamingMessage,
            streaming: this.agent.state.isStreaming,
            pendingToolCalls: this.agent.state.pendingToolCalls,
            toolProgress: new Map(this.toolProgress),
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
