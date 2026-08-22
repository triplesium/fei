import type { ProxyAssistantMessageEvent } from "@earendil-works/pi-agent-core";
import type { AssistantMessageEvent } from "@earendil-works/pi-ai";

export function toProxyEvent(event: AssistantMessageEvent): ProxyAssistantMessageEvent {
    switch (event.type) {
        case "start":
            return { type: "start" };
        case "text_start":
            return { type: "text_start", contentIndex: event.contentIndex };
        case "text_delta":
            return { type: "text_delta", contentIndex: event.contentIndex, delta: event.delta };
        case "text_end": {
            const content = event.partial.content[event.contentIndex];
            return {
                type: "text_end",
                contentIndex: event.contentIndex,
                contentSignature: content?.type === "text" ? content.textSignature : undefined,
            };
        }
        case "thinking_start":
            return { type: "thinking_start", contentIndex: event.contentIndex };
        case "thinking_delta":
            return { type: "thinking_delta", contentIndex: event.contentIndex, delta: event.delta };
        case "thinking_end": {
            const content = event.partial.content[event.contentIndex];
            return {
                type: "thinking_end",
                contentIndex: event.contentIndex,
                contentSignature:
                    content?.type === "thinking" ? content.thinkingSignature : undefined,
            };
        }
        case "toolcall_start": {
            const content = event.partial.content[event.contentIndex];
            if (content?.type !== "toolCall") {
                throw new Error("Pi emitted toolcall_start without a tool call.");
            }
            return {
                type: "toolcall_start",
                contentIndex: event.contentIndex,
                id: content.id,
                toolName: content.name,
            };
        }
        case "toolcall_delta":
            return { type: "toolcall_delta", contentIndex: event.contentIndex, delta: event.delta };
        case "toolcall_end":
            return {
                type: "toolcall_end",
                contentIndex: event.contentIndex,
                toolCall: event.toolCall,
            };
        case "done":
            if (event.reason === "deferred") {
                throw new Error("Deferred model responses are not supported by the Editor proxy.");
            }
            return { type: "done", reason: event.reason, usage: event.message.usage };
        case "error":
            return {
                type: "error",
                reason: event.reason,
                errorMessage: event.error.errorMessage,
                usage: event.error.usage,
            };
    }
}
