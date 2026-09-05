import {
    fauxAssistantMessage,
    fauxThinking,
    fauxText,
    fauxToolCall,
    type ToolResultMessage,
    type UserMessage,
} from "@earendil-works/pi-ai";
import { describe, expect, it } from "vitest";
import type { EditorPiAgentSnapshot } from "./editor-pi-agent";
import { projectPiMessages } from "./pi-assistant-messages";

describe("projectPiMessages", () => {
    it("projects transient progress without adding model messages", () => {
        const assistant = fauxAssistantMessage(fauxToolCall("play_segment", {}, { id: "progress-tool" }), { stopReason: "toolUse" });
        const snapshot: EditorPiAgentSnapshot = {
            messages: [assistant], streaming: true, configured: true,
            pendingToolCalls: new Set(["progress-tool"]),
            toolProgress: new Map([["progress-tool", "Playtest running: 12 / 60 ticks"]]),
        };
        const projected = projectPiMessages(snapshot);
        expect(projected[0]).toMatchObject({ content: [{
            type: "tool-call", result: { inProgress: true, message: "Playtest running: 12 / 60 ticks" },
        }] });
        expect(snapshot.messages).toEqual([assistant]);
    });
    it("embeds Pi reasoning and tool results in assistant-ui message parts", () => {
        const user: UserMessage = {
            role: "user",
            content: "Check the runtime.",
            timestamp: 1,
        };
        const assistant = fauxAssistantMessage(
            [
                fauxThinking("I should inspect the runtime first."),
                fauxToolCall("runtime_status", {}, { id: "tool-1" }),
            ],
            { stopReason: "toolUse", timestamp: 2 },
        );
        const toolResult: ToolResultMessage = {
            role: "toolResult",
            toolCallId: "tool-1",
            toolName: "runtime_status",
            content: [fauxText('{"state":"stopped"}')],
            details: { state: "stopped" },
            isError: false,
            timestamp: 3,
        };
        const final = fauxAssistantMessage("The runtime is stopped.", { timestamp: 4 });
        const snapshot: EditorPiAgentSnapshot = {
            messages: [user, assistant, toolResult, final],
            streaming: false,
            pendingToolCalls: new Set(),
            configured: true,
        };

        const projected = projectPiMessages(snapshot);

        expect(projected).toHaveLength(2);
        expect(projected[1]).toMatchObject({
            role: "assistant",
            content: [
                { type: "reasoning", text: "I should inspect the runtime first." },
                {
                    type: "tool-call",
                    toolCallId: "tool-1",
                    toolName: "runtime_status",
                    args: {},
                    result: { state: "stopped" },
                    isError: false,
                },
                { type: "text", text: "The runtime is stopped." },
            ],
            status: { type: "complete" },
        });
    });

    it("marks a partial streamed assistant message as running", () => {
        const streamingMessage = fauxAssistantMessage("Working", {
            stopReason: "pending",
            timestamp: 5,
        });
        const projected = projectPiMessages({
            messages: [],
            streamingMessage,
            streaming: true,
            pendingToolCalls: new Set(),
            configured: true,
        });

        expect(projected[0]).toMatchObject({
            role: "assistant",
            status: { type: "running" },
        });
    });

    it("keeps assistant turns separated by user messages", () => {
        const firstUser: UserMessage = { role: "user", content: "First", timestamp: 1 };
        const secondUser: UserMessage = { role: "user", content: "Second", timestamp: 3 };
        const projected = projectPiMessages({
            messages: [
                firstUser,
                fauxAssistantMessage("First answer", { timestamp: 2 }),
                secondUser,
                fauxAssistantMessage("Second answer", { timestamp: 4 }),
            ],
            streaming: false,
            pendingToolCalls: new Set(),
            configured: true,
        });

        expect(projected.map((message) => message.role)).toEqual([
            "user",
            "assistant",
            "user",
            "assistant",
        ]);
    });
});
