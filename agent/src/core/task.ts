import type { AgentEvent, AgentTool } from "@earendil-works/pi-agent-core";
import { EntisiumAgent, type AgentConfiguration } from "./agent.js";

/** One task, with injected model/tools and an awaited ownership cleanup boundary. */
export async function runAgentTask(options: {
    prompt: string;
    followUps?: string[];
    onTurnEnd?: (turn: number) => Promise<void>;
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
        const prompts = [options.prompt, ...(options.followUps ?? [])];
        for (let turn = 0; turn < prompts.length; turn++) {
            options.signal?.throwIfAborted();
            await agent.prompt(prompts[turn]);
            options.signal?.throwIfAborted();
            const error = agent.snapshot().error;
            if (error) throw new Error(error);
            await options.onTurnEnd?.(turn);
        }
    } finally {
        options.signal?.removeEventListener("abort", cancel);
        unsubscribe();
        agent.dispose();
        await options.cleanup();
    }
}
