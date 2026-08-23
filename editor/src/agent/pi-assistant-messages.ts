import type { AgentMessage } from "@earendil-works/pi-agent-core";
import type { AssistantMessage, ToolResultMessage, UserMessage } from "@earendil-works/pi-ai";
import type {
    MessageStatus,
    ThreadAssistantMessagePart,
    ThreadMessageLike,
} from "@assistant-ui/react";
import type { EditorPiAgentSnapshot } from "./editor-pi-agent";

interface ProjectedAssistantMessage {
    id: string;
    role: "assistant";
    content: ThreadAssistantMessagePart[];
    createdAt: Date;
    status: MessageStatus;
}

function dataUrl(data: string, mimeType: string): string {
    return data.startsWith("data:") ? data : `data:${mimeType};base64,${data}`;
}

function messageId(message: AgentMessage, index: number): string {
    return `pi-${message.role}-${message.timestamp}-${index}`;
}

function toolResultValue(message: ToolResultMessage): unknown {
    if (message.details !== undefined) return message.details;
    const text = message.content
        .filter((part) => part.type === "text")
        .map((part) => part.text)
        .join("\n");
    return text || (message.isError ? "Tool execution failed." : "Tool execution completed.");
}

function assistantStatus(message: AssistantMessage, streaming: boolean): MessageStatus {
    if (streaming || message.stopReason === "pending") return { type: "running" };
    switch (message.stopReason) {
        case "stop":
            return { type: "complete", reason: "stop" };
        case "aborted":
            return { type: "incomplete", reason: "cancelled" };
        case "length":
            return { type: "incomplete", reason: "length" };
        case "error":
            return {
                type: "incomplete",
                reason: "error",
                error: message.errorMessage ?? "The model request failed.",
            };
        default:
            return { type: "complete", reason: "unknown" };
    }
}

function projectUserMessage(message: UserMessage, index: number): ThreadMessageLike {
    const content =
        typeof message.content === "string"
            ? [{ type: "text" as const, text: message.content }]
            : message.content.map((part) =>
                  part.type === "text"
                      ? { type: "text" as const, text: part.text }
                      : {
                            type: "image" as const,
                            image: dataUrl(part.data, part.mimeType),
                        },
              );
    return {
        id: messageId(message, index),
        role: "user",
        content,
        createdAt: new Date(message.timestamp),
    };
}

function projectAssistantMessage(
    message: AssistantMessage,
    index: number,
    streaming: boolean,
    toolResults: ReadonlyMap<string, ToolResultMessage>,
): ProjectedAssistantMessage {
    const content: ThreadAssistantMessagePart[] = [];
    for (const part of message.content) {
        if (part.type === "text") {
            content.push({ type: "text", text: part.text });
            continue;
        }
        if (part.type === "thinking") {
            if (!part.redacted) content.push({ type: "reasoning", text: part.thinking });
            continue;
        }
        const result = toolResults.get(part.id);
        content.push({
            type: "tool-call",
            toolCallId: part.id,
            toolName: part.name,
            args: part.arguments,
            argsText: JSON.stringify(part.arguments),
            result: result ? toolResultValue(result) : undefined,
            isError: result?.isError,
        });
    }
    return {
        id: messageId(message, index),
        role: "assistant",
        content,
        createdAt: new Date(message.timestamp),
        status: assistantStatus(message, streaming),
    };
}

function projectAssistantTurn(
    messages: readonly { message: AssistantMessage; index: number; streaming: boolean }[],
    toolResults: ReadonlyMap<string, ToolResultMessage>,
): ProjectedAssistantMessage {
    const projected = messages.map(({ message, index, streaming }) =>
        projectAssistantMessage(message, index, streaming, toolResults),
    );
    const first = projected[0]!;
    const last = projected.at(-1)!;
    return {
        ...first,
        content: projected.flatMap((message) => message.content),
        status: last.status,
    };
}

export function projectPiMessages(snapshot: EditorPiAgentSnapshot): ThreadMessageLike[] {
    const messages = [...snapshot.messages];
    if (snapshot.streamingMessage && !messages.includes(snapshot.streamingMessage)) {
        messages.push(snapshot.streamingMessage);
    }
    const toolResults = new Map<string, ToolResultMessage>();
    for (const message of messages) {
        if (message.role === "toolResult") toolResults.set(message.toolCallId, message);
    }
    const projected: ThreadMessageLike[] = [];
    let assistantTurn: { message: AssistantMessage; index: number; streaming: boolean }[] = [];
    const flushAssistantTurn = (): void => {
        if (assistantTurn.length === 0) return;
        projected.push(projectAssistantTurn(assistantTurn, toolResults));
        assistantTurn = [];
    };

    messages.forEach((message, index) => {
        if (message.role === "assistant") {
            assistantTurn.push({
                message,
                index,
                streaming: message === snapshot.streamingMessage,
            });
            return;
        }
        if (message.role === "toolResult") return;
        flushAssistantTurn();
        if (message.role === "user") projected.push(projectUserMessage(message, index));
    });
    flushAssistantTurn();
    return projected;
}
