import {
    createModels,
    fauxAssistantMessage,
    fauxProvider,
    fauxToolCall,
} from "@earendil-works/pi-ai";
import { describe, expect, it } from "vitest";
import type { EditorAgentApi } from "../types";
import { EditorPiAgent } from "./editor-pi-agent";

describe("EditorPiAgent", () => {
    it("keeps playtest progress in UI snapshots and sends the model one terminal result", async () => {
        let requestId: unknown;
        const editor: EditorAgentApi = {
            capabilities: [],
            invoke: async (request) => {
                if (request.provider === "play.segment") requestId = (request.payload as { request_id: string }).request_id;
                return { requestId: "reply", ok: true, value: {
                    request_id: requestId,
                    state: request.provider === "play.segment" ? "pending" : "stopped",
                    completed_ticks: request.provider === "play.segment" ? 0 : 3,
                    max_ticks: 10,
                } };
            },
        };
        const faux = fauxProvider();
        faux.setResponses([
            fauxAssistantMessage(fauxToolCall("play_segment", {
                interface: "game.main", source: "return function() return {stop='done'} end", maxTicks: 10,
            }), { stopReason: "toolUse" }),
            fauxAssistantMessage("Done."),
        ]);
        const models = createModels();
        models.setProvider(faux.provider);
        const agent = new EditorPiAgent(editor);
        agent.configure({ model: faux.getModel(), streamFn: models.streamSimple.bind(models) });
        const progress: string[] = [];
        agent.subscribeState(() => progress.push(...agent.snapshot().toolProgress!.values()));
        try {
            await agent.prompt("Run a short playtest.");
            expect(progress.some((text) => text.includes("0 / 10 ticks"))).toBe(true);
            const results = agent.snapshot().messages.filter((message) => message.role === "toolResult");
            expect(results).toHaveLength(1);
            expect(JSON.stringify(results[0])).toContain("stopped");
            expect(JSON.stringify(results[0])).not.toContain("pending");
            expect(agent.snapshot().toolProgress?.size).toBe(0);
        } finally { agent.dispose(); }
    });
    it("executes runtime_status through the Editor command bus", async () => {
        const commands: string[] = [];
        const editor: EditorAgentApi = {
            capabilities: ["runtime.status"],
            invoke: async (request) => {
                commands.push(request.type ?? "");
                return {
                    requestId: "editor-response",
                    ok: true,
                    value: { state: "stopped", script: "—", frame: "—" },
                };
            },
        };
        const faux = fauxProvider();
        faux.setResponses([
            fauxAssistantMessage(fauxToolCall("runtime_status", {}), {
                stopReason: "toolUse",
            }),
            fauxAssistantMessage("The WebAssembly runtime is stopped."),
        ]);
        const models = createModels();
        models.setProvider(faux.provider);

        const agent = new EditorPiAgent(editor);
        agent.configure({
            model: faux.getModel(),
            streamFn: models.streamSimple.bind(models),
        });

        const completedTools: string[] = [];
        const streamingStates: boolean[] = [];
        const unsubscribeState = agent.subscribeState(() => {
            streamingStates.push(agent.snapshot().streaming);
        });
        const unsubscribe = agent.subscribe((event) => {
            if (event.type === "tool_execution_end" && !event.isError) {
                completedTools.push(event.toolName);
            }
        });

        await agent.prompt("Check the runtime status.");

        expect(commands).toEqual(["runtime.status"]);
        expect(completedTools).toEqual(["runtime_status"]);
        expect(streamingStates).toContain(true);
        expect(streamingStates.at(-1)).toBe(false);
        expect(agent.status()).toMatchObject({
            configured: true,
            streaming: false,
            tools: [
                "project_list",
                "project_read",
                "project_write",
                "project_create",
                "runtime_status",
                "runtime_observe",
                "runtime_key",
                "runtime_pointer",
                "runtime_wait",
                "runtime_clear_input",
                "runtime_logs",
                "profiler_summary",
                "profiler_frames",
                "profiler_frame",
                "play_interfaces",
                "play_observe",
                "play_step",
                "play_segment",
                "play_segment_cancel",
                "runtime_play",
                "runtime_stop",
                "runtime_restart",
            ],
        });

        unsubscribe();
        unsubscribeState();
        agent.dispose();
    });
});
