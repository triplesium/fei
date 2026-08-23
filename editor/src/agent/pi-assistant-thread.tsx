import {
    ActionBarPrimitive,
    AssistantRuntimeProvider,
    ComposerPrimitive,
    ErrorPrimitive,
    MessagePrimitive,
    ThreadPrimitive,
    groupPartByType,
    useExternalStoreRuntime,
    type AppendMessage,
    type TextMessagePartProps,
    type ThreadMessageLike,
} from "@assistant-ui/react";
import {
    ArrowDown,
    ArrowUp,
    Check,
    Copy,
    LoaderCircle,
    Square,
    SquarePen,
} from "lucide-react";
import { useCallback, useEffect, useMemo, useReducer, type ReactNode } from "react";
import { MarkdownText } from "@/components/assistant-ui/markdown-text";
import { Reasoning } from "@/components/assistant-ui/reasoning";
import { ToolFallback } from "@/components/assistant-ui/tool-fallback";
import { ToolGroup } from "@/components/assistant-ui/tool-group";
import { TooltipIconButton } from "@/components/assistant-ui/tooltip-icon-button";
import { Button } from "@/components/ui/button";
import type { EditorPiAgent } from "./editor-pi-agent";
import { projectPiMessages } from "./pi-assistant-messages";
import type { AgentDensity } from "@/services/editor-host-client";

function appendMessageText(message: AppendMessage): string {
    if (message.role !== "user") return "";
    return message.content
        .filter((part) => part.type === "text")
        .map((part) => part.text)
        .join("\n")
        .trim();
}

function UserText({ text }: TextMessagePartProps) {
    return <span className="whitespace-pre-wrap">{text}</span>;
}

function UserMessage() {
    return (
        <MessagePrimitive.Root
            data-role="user"
            className="grid grid-cols-[minmax(48px,1fr)_auto] px-2 [&>*]:col-start-2"
        >
            <div className="col-start-2 min-w-0 max-w-full rounded-xl bg-muted px-3.5 py-2 text-sm text-foreground">
                <MessagePrimitive.Parts components={{ Text: UserText }} />
            </div>
        </MessagePrimitive.Root>
    );
}

function AssistantActionBar() {
    return (
        <ActionBarPrimitive.Root
            hideWhenRunning
            autohide="always"
            className="ml-1 mt-1 flex h-6 items-center text-muted-foreground"
        >
            <ActionBarPrimitive.Copy
                className="grid size-6 place-items-center rounded-md border-0 bg-transparent p-0 outline-none transition-colors hover:bg-muted hover:text-foreground focus-visible:ring-1 focus-visible:ring-ring/40"
                aria-label="Copy message"
            >
                <MessagePrimitive.If copied>
                    <Check className="size-3.5" />
                </MessagePrimitive.If>
                <MessagePrimitive.If copied={false}>
                    <Copy className="size-3.5" />
                </MessagePrimitive.If>
            </ActionBarPrimitive.Copy>
        </ActionBarPrimitive.Root>
    );
}

function AssistantMessage() {
    return (
        <MessagePrimitive.Root data-role="assistant" className="relative px-2">
            <div className="px-2 leading-relaxed text-foreground [overflow-wrap:anywhere]">
                <MessagePrimitive.GroupedParts
                    groupBy={groupPartByType({
                        reasoning: ["group-chain-of-thought", "group-reasoning"],
                        "tool-call": ["group-chain-of-thought", "group-tools"],
                        "standalone-tool-call": [],
                    })}
                >
                    {({ part, children }) => {
                        switch (part.type) {
                            case "group-chain-of-thought":
                                return <div className="aui-chain-of-thought mt-2 mb-2 first:mt-0">{children}</div>;
                            case "group-reasoning":
                                return children;
                            case "group-tools":
                                return (
                                    <ToolGroup
                                        active={part.status.type === "running"}
                                        failed={part.status.type === "incomplete"}
                                    >
                                        {children}
                                    </ToolGroup>
                                );
                            case "text":
                                return <MarkdownText />;
                            case "reasoning":
                                return <Reasoning {...part} />;
                            case "tool-call":
                                return part.toolUI ?? <ToolFallback {...part} />;
                            case "indicator":
                                return <LoaderCircle className="my-3 size-4 animate-spin text-muted-foreground" />;
                            default:
                                return null;
                        }
                    }}
                </MessagePrimitive.GroupedParts>
                <MessagePrimitive.Error>
                    <div className="mt-2 rounded-md border border-destructive bg-destructive/10 p-3 text-sm text-destructive">
                        <ErrorPrimitive.Message />
                    </div>
                </MessagePrimitive.Error>
            </div>
            <AssistantActionBar />
        </MessagePrimitive.Root>
    );
}

export interface PiAssistantThreadProps {
    agent: EditorPiAgent;
    enabled: boolean;
    modelControl?: ReactNode;
    density?: AgentDensity;
    onConfigure(): void;
    onNewChat(): void;
}

export function PiAssistantThread({
    agent,
    enabled,
    modelControl,
    density = "compact",
    onConfigure,
    onNewChat,
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
            <ThreadPrimitive.Root
                data-agent-density={density}
                className="flex min-h-0 flex-1 flex-col overflow-hidden bg-background"
                style={{ ["--thread-max-width" as string]: "44rem" }}
            >
                <div className="flex h-9 shrink-0 items-center justify-between border-b border-border/60 px-3">
                    <span className="text-[11px] font-medium text-muted-foreground">Entisium Agent</span>
                    <ThreadPrimitive.If empty={false}>
                        <TooltipIconButton
                            tooltip="New chat"
                            aria-label="New chat"
                            className="size-7 rounded-md"
                            type="button"
                            disabled={snapshot.streaming}
                            onClick={onNewChat}
                        >
                            <SquarePen className="size-3.5" />
                        </TooltipIconButton>
                    </ThreadPrimitive.If>
                </div>
                <ThreadPrimitive.Viewport
                    turnAnchor="top"
                    className="relative flex min-h-0 flex-1 flex-col overflow-x-hidden overflow-y-auto scroll-smooth px-2 pt-4 [scrollbar-gutter:stable]"
                    aria-live="polite"
                >
                    <ThreadPrimitive.Empty>
                        <div className="flex min-h-[240px] flex-1 flex-col items-center justify-center gap-2 px-6 pb-8 text-center">
                            <h2 className="m-0 text-lg font-semibold text-foreground">How can I help you today?</h2>
                            {!enabled && (
                                <Button variant="outline" size="sm" type="button" className="mt-2 rounded-full" onClick={onConfigure}>
                                    Configure model
                                </Button>
                            )}
                        </div>
                    </ThreadPrimitive.Empty>
                    <ThreadPrimitive.Messages
                        components={{ UserMessage, AssistantMessage }}
                    />
                    <ThreadPrimitive.ViewportFooter className="sticky bottom-0 z-[1] mt-auto flex w-full max-w-(--thread-max-width) flex-col bg-background px-0 pt-4 pb-2">
                        <ThreadPrimitive.If empty={false}>
                            <ThreadPrimitive.ScrollToBottom
                                className="absolute -top-7 self-center rounded-full border border-border/60 bg-background p-2 text-muted-foreground shadow-sm outline-none transition-colors hover:bg-muted hover:text-foreground focus-visible:ring-1 focus-visible:ring-ring/40 disabled:invisible"
                                aria-label="Scroll to bottom"
                            >
                                <ArrowDown className="size-3.5" />
                            </ThreadPrimitive.ScrollToBottom>
                        </ThreadPrimitive.If>
                        <ComposerPrimitive.Root className="relative mx-1.5 flex shrink-0 flex-col gap-1.5 rounded-[1.5rem] border border-border/60 bg-[color-mix(in_oklab,var(--color-muted)_30%,var(--color-background))] p-2 shadow-[0_4px_16px_-8px_rgb(0_0_0/0.35),0_1px_2px_rgb(0_0_0/0.15)] transition-[border-color,box-shadow] focus-within:border-border">
                            <ComposerPrimitive.Input
                                className="min-h-10 max-h-32 w-full resize-none border-0 bg-transparent px-2.5 py-1 text-sm leading-5 text-foreground outline-none placeholder:text-muted-foreground/80"
                                rows={1}
                                submitMode="enter"
                                placeholder={enabled ? "Send a message..." : "Configure a model to chat"}
                                aria-label="Message input"
                            />
                            <div className="relative flex min-h-7 items-center justify-between gap-2">
                                <div className="flex min-w-0 items-center">{modelControl}</div>
                                <div className="flex items-center gap-1.5">
                                    <ThreadPrimitive.If running>
                                        <ComposerPrimitive.Cancel
                                            className="grid size-7 place-items-center rounded-full border-0 bg-primary p-0 text-primary-foreground outline-none transition-colors hover:bg-primary/80 focus-visible:ring-2 focus-visible:ring-ring/35"
                                            aria-label="Stop generating"
                                        >
                                            <Square className="size-3.5" fill="currentColor" />
                                        </ComposerPrimitive.Cancel>
                                    </ThreadPrimitive.If>
                                    <ThreadPrimitive.If running={false}>
                                        <ComposerPrimitive.Send
                                            className="grid size-7 place-items-center rounded-full border-0 bg-primary p-0 text-primary-foreground outline-none transition-colors hover:bg-primary/80 focus-visible:ring-2 focus-visible:ring-ring/35 disabled:bg-muted disabled:text-muted-foreground"
                                            aria-label="Send message"
                                        >
                                            <ArrowUp className="size-[18px]" />
                                        </ComposerPrimitive.Send>
                                    </ThreadPrimitive.If>
                                </div>
                            </div>
                        </ComposerPrimitive.Root>
                    </ThreadPrimitive.ViewportFooter>
                </ThreadPrimitive.Viewport>
            </ThreadPrimitive.Root>
        </AssistantRuntimeProvider>
    );
}
