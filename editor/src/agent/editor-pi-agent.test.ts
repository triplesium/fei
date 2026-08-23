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
