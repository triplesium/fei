import type { AgentEvent, AgentTool } from "@earendil-works/pi-agent-core";
import { EntisiumAgent, type AgentConfiguration } from "./agent.js";

/** One task, with injected model/tools and an awaited ownership cleanup boundary. */
export async function runAgentTask(options: {
    prompt: string;
    configuration: AgentConfiguration;
    tools: AgentTool<any>[];
    signal?: AbortSignal;
    onEvent?: (event: AgentEvent) => void | Promise<void>;
    cleanup(): Promise<void>;
}): Promise<void> {
    const agent = new EntisiumAgent(options.tools);
    const cancel = () => agent.abort();
    const unsubscribe = options.onEvent ? agent.subscribe(options.onEvent) : () => {};
    try {
        options.signal?.throwIfAborted();
        agent.configure(options.configuration);
        options.signal?.addEventListener("abort", cancel, { once: true });
        await agent.prompt(options.prompt);
        options.signal?.throwIfAborted();
        const error = agent.snapshot().error;
        if (error) throw new Error(error);
    } finally {
        options.signal?.removeEventListener("abort", cancel);
        unsubscribe();
        agent.dispose();
        await options.cleanup();
    }
}
