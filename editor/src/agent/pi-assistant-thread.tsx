import {
    AssistantRuntimeProvider,
    ComposerPrimitive,
    ErrorPrimitive,
    MessagePrimitive,
    ThreadPrimitive,
    useExternalStoreRuntime,
    type AppendMessage,
    type ReasoningMessagePartProps,
    type TextMessagePartProps,
    type ThreadMessageLike,
    type ToolCallMessagePartProps,
} from "@assistant-ui/react";
import {
    Bot,
    Check,
    ChevronRight,
    CircleAlert,
    LoaderCircle,
    Send,
    Sparkles,
    Square,
} from "lucide-react";
import { useCallback, useEffect, useMemo, useReducer, type ReactNode } from "react";
import type { EditorPiAgent } from "./editor-pi-agent";
import { projectPiMessages } from "./pi-assistant-messages";

function appendMessageText(message: AppendMessage): string {
    if (message.role !== "user") return "";
    return message.content
        .filter((part) => part.type === "text")
        .map((part) => part.text)
        .join("\n")
        .trim();
}

function formatValue(value: unknown): string {
    if (typeof value === "string") return value;
    try {
        return JSON.stringify(value, null, 2);
    } catch {
        return String(value);
    }
}

function inlineMarkdown(text: string, keyPrefix: string): ReactNode[] {
    return text
        .split(/(`[^`]+`|\*\*[^*]+\*\*)/g)
        .filter(Boolean)
        .map((part, index) => {
            const key = `${keyPrefix}-${index}`;
            if (part.startsWith("**") && part.endsWith("**")) {
                return <strong key={key}>{part.slice(2, -2)}</strong>;
            }
            if (part.startsWith("`") && part.endsWith("`")) {
                return <code key={key}>{part.slice(1, -1)}</code>;
            }
            return part;
        });
}

function TextPart({ text }: TextMessagePartProps) {
    const lines = text.replaceAll("\r\n", "\n").split("\n");
    const blocks: ReactNode[] = [];
    let index = 0;
    while (index < lines.length) {
        const line = lines[index] ?? "";
        if (!line.trim()) {
            index += 1;
            continue;
        }
        if (line.trimStart().startsWith("```")) {
            const code: string[] = [];
            index += 1;
            while (index < lines.length && !(lines[index] ?? "").trimStart().startsWith("```")) {
                code.push(lines[index] ?? "");
                index += 1;
            }
            index += 1;
            blocks.push(<pre key={`code-${index}`}><code>{code.join("\n")}</code></pre>);
            continue;
        }
        const heading = /^(#{1,3})\s+(.+)$/.exec(line.trim());
        if (heading) {
            blocks.push(
                <strong className="agent-markdown-heading" key={`heading-${index}`}>
                    {inlineMarkdown(heading[2], `heading-${index}`)}
                </strong>,
            );
            index += 1;
            continue;
        }
        if (/^\s*[-*]\s+/.test(line)) {
            const items: ReactNode[] = [];
            while (index < lines.length && /^\s*[-*]\s+/.test(lines[index] ?? "")) {
                const item = (lines[index] ?? "").replace(/^\s*[-*]\s+/, "");
                items.push(<li key={`item-${index}`}>{inlineMarkdown(item, `item-${index}`)}</li>);
                index += 1;
            }
            blocks.push(<ul key={`list-${index}`}>{items}</ul>);
            continue;
        }
        const paragraph: string[] = [];
        while (
            index < lines.length &&
            (lines[index] ?? "").trim() &&
            !/^\s*[-*]\s+/.test(lines[index] ?? "") &&
            !/^(#{1,3})\s+/.test((lines[index] ?? "").trim()) &&
            !(lines[index] ?? "").trimStart().startsWith("```")
        ) {
            paragraph.push((lines[index] ?? "").trim());
            index += 1;
        }
        blocks.push(
            <p key={`paragraph-${index}`}>
                {inlineMarkdown(paragraph.join(" "), `paragraph-${index}`)}
            </p>,
        );
    }
    return <div className="agent-markdown">{blocks}</div>;
}

function ReasoningPart({ text, status }: ReasoningMessagePartProps) {
    return (
        <details className="agent-reasoning" open={status.type === "running"}>
            <summary>
                <Sparkles size={12} />
                <span>{status.type === "running" ? "Thinking…" : "Thought process"}</span>
                <ChevronRight className="agent-disclosure" size={11} />
            </summary>
            <div>{text}</div>
        </details>
    );
}

function ToolCallPart({ toolName, args, result, isError }: ToolCallMessagePartProps) {
    const completed = result !== undefined;
    const StatusIcon = completed ? (isError ? CircleAlert : Check) : LoaderCircle;
    return (
        <details className={`agent-tool-call${isError ? " error" : ""}`} open={!completed}>
            <summary>
                <StatusIcon className={completed ? "" : "agent-tool-spinner"} size={12} />
                <span>{toolName}</span>
                <em>{completed ? (isError ? "Failed" : "Completed") : "Running…"}</em>
                <ChevronRight className="agent-disclosure" size={11} />
            </summary>
            <div className="agent-tool-section">
                <span>Arguments</span>
                <pre>{formatValue(args)}</pre>
            </div>
            {completed && (
                <div className="agent-tool-section">
                    <span>Result</span>
                    <pre>{formatValue(result)}</pre>
                </div>
            )}
        </details>
    );
}

function UserMessage() {
    return (
        <MessagePrimitive.Root className="agent-message user">
            <div className="agent-message-role"><span>You</span></div>
            <MessagePrimitive.Parts components={{ Text: TextPart }} />
        </MessagePrimitive.Root>
    );
}

function AssistantMessage() {
    return (
        <MessagePrimitive.Root className="agent-message assistant">
            <div className="agent-message-role">
                <span className="agent-avatar"><Bot size={12} /></span>
                <span>Fei</span>
            </div>
            <MessagePrimitive.Parts
                components={{
                    Text: TextPart,
                    Reasoning: ReasoningPart,
                    tools: { Fallback: ToolCallPart },
                }}
            />
            <MessagePrimitive.Error>
                <div className="agent-message-error">
                    <ErrorPrimitive.Message />
                </div>
            </MessagePrimitive.Error>
        </MessagePrimitive.Root>
    );
}

export interface PiAssistantThreadProps {
    agent: EditorPiAgent;
    enabled: boolean;
    model?: string;
    onConfigure(): void;
}

export function PiAssistantThread({
    agent,
    enabled,
    model,
    onConfigure,
}: PiAssistantThreadProps) {
    const [version, refresh] = useReducer((value: number) => value + 1, 0);
    useEffect(() => agent.subscribeState(refresh), [agent]);

    const snapshot = agent.snapshot();
    const messages = useMemo<readonly ThreadMessageLike[]>(
        () => projectPiMessages(snapshot),
        // The Pi subscription increments version after every state transition and stream update.
        // eslint-disable-next-line react-hooks/exhaustive-deps
        [agent, version],
    );
    const onNew = useCallback(
        async (message: AppendMessage) => {
            const text = appendMessageText(message);
            if (!text) return;
            await agent.prompt(text);
        },
        [agent],
    );
    const onCancel = useCallback(async () => agent.abort(), [agent]);
    const runtime = useExternalStoreRuntime<ThreadMessageLike>({
        messages,
        convertMessage: (message) => message,
        isRunning: snapshot.streaming,
        isSendDisabled: !enabled || !snapshot.configured,
        onNew,
        onCancel,
    });

    return (
        <AssistantRuntimeProvider runtime={runtime}>
            <ThreadPrimitive.Root className="aui-thread">
                <ThreadPrimitive.Viewport className="aui-thread-viewport" aria-live="polite">
                    <ThreadPrimitive.Empty>
                        <div className="agent-empty">
                            <Bot size={24} strokeWidth={1.4} />
                            <strong>Fei Agent</strong>
                            <span>
                                {enabled
                                    ? `Ask ${model ?? "the agent"} to inspect or control the WebAssembly runtime.`
                                    : "Configure DeepSeek to start a conversation."}
                            </span>
                            {!enabled && (
                                <button className="button" type="button" onClick={onConfigure}>
                                    Configure model
                                </button>
                            )}
                        </div>
                    </ThreadPrimitive.Empty>
                    <ThreadPrimitive.Messages
                        components={{ UserMessage, AssistantMessage }}
                    />
                    <ThreadPrimitive.ViewportFooter className="aui-thread-footer">
                        <ComposerPrimitive.Root className="agent-composer aui-composer">
                            <ComposerPrimitive.Input
                                className="text-area"
                                rows={1}
                                submitMode="enter"
                                placeholder={enabled ? "Ask Fei Agent…" : "Configure a model to chat"}
                            />
                            <ThreadPrimitive.If running>
                                <ComposerPrimitive.Cancel
                                    className="agent-send agent-cancel"
                                    aria-label="Stop agent"
                                >
                                    <Square size={11} fill="currentColor" />
                                </ComposerPrimitive.Cancel>
                            </ThreadPrimitive.If>
                            <ThreadPrimitive.If running={false}>
                                <ComposerPrimitive.Send className="agent-send" aria-label="Send message">
                                    <Send size={14} />
                                </ComposerPrimitive.Send>
                            </ThreadPrimitive.If>
                        </ComposerPrimitive.Root>
                    </ThreadPrimitive.ViewportFooter>
                </ThreadPrimitive.Viewport>
            </ThreadPrimitive.Root>
        </AssistantRuntimeProvider>
    );
}
