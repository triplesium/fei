import { describe, expect, it, vi } from "vitest";
import { createModels, fauxAssistantMessage, fauxProvider, fauxToolCall } from "@earendil-works/pi-ai";
import { createNativeAgentTools } from "../src/tools/runtime.js";
import { runAgentTask } from "../src/core/task.js";

function configuration(responses: ReturnType<typeof fauxAssistantMessage>[]) {
    const faux = fauxProvider();
    faux.setResponses(responses);
    const models = createModels(); models.setProvider(faux.provider);
    return { model: faux.getModel(), streamFn: models.streamSimple.bind(models) };
}

describe("headless agent task", () => {
    it("retains conversation history and awaits snapshots across follow-up turns", async () => {
        const base = configuration([fauxAssistantMessage("Speed is now 8."), fauxAssistantMessage("Jump is now 6; speed remains 8.")]);
        const contexts: string[] = [];
        const turns: number[] = [];
        const cleanup = vi.fn(async () => {});
        await runAgentTask({ prompt: "Set speed to 8.", followUps: ["Now set jump to 6."], tools: [], cleanup,
            configuration: { ...base, streamFn: (...args) => {
                contexts.push(JSON.stringify(args[1])); return base.streamFn(...args);
            } },
            onTurnEnd: async turn => { await Promise.resolve(); turns.push(turn); },
        });
        expect(contexts[1]).toContain("Set speed to 8.");
        expect(contexts[1]).toContain("Speed is now 8.");
        expect(contexts[1]).toContain("Now set jump to 6.");
        expect(turns).toEqual([0, 1]);
        expect(cleanup).toHaveBeenCalledOnce();
    });
    it("executes injected tools and emits the final answer without browser globals", async () => {
        const invoke = vi.fn(async () => ({ value: { state: "stopped" } }));
        const cleanup = vi.fn(async () => {});
        const events: string[] = [];
        await runAgentTask({ prompt: "Check native status", configuration: configuration([
            fauxAssistantMessage(fauxToolCall("native_runtime_status", {}), { stopReason: "toolUse" }),
            fauxAssistantMessage("Stopped."),
        ]), tools: createNativeAgentTools(invoke), cleanup, onEvent: (event) => { events.push(event.type); } });
        expect(invoke).toHaveBeenCalledWith("runtime_status", {}, expect.any(AbortSignal));
        expect(events).toContain("tool_execution_end");
        expect(events).toContain("agent_end");
        expect(cleanup).toHaveBeenCalledOnce();
    });
    it("awaits cleanup when the model fails", async () => {
        const cleanup = vi.fn(async () => {});
        await expect(runAgentTask({ prompt: "test", tools: [], cleanup,
            configuration: configuration([fauxAssistantMessage("", { stopReason: "error", errorMessage: "model unavailable" })]),
        })).rejects.toThrow("model unavailable");
        expect(cleanup).toHaveBeenCalledOnce();
    });
    it("cancels an executing tool and awaits ownership cleanup", async () => {
        const controller = new AbortController();
        let stopped = false;
        const invoke = vi.fn(async (_name, _input, signal?: AbortSignal) => {
            controller.abort(new Error("cancelled"));
            signal?.throwIfAborted();
            return { value: null };
        });
        await expect(runAgentTask({ prompt: "start", signal: controller.signal,
            configuration: configuration([fauxAssistantMessage(fauxToolCall("native_runtime_play", {}), { stopReason: "toolUse" })]),
            tools: createNativeAgentTools(invoke), cleanup: async () => { await Promise.resolve(); stopped = true; },
        })).rejects.toThrow("cancelled");
        expect(stopped).toBe(true);
    });
});
