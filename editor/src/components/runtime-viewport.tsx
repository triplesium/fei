import { LoaderCircle, Play, RotateCcw, TriangleAlert } from "lucide-react";
import type { RuntimeSession, RuntimeState } from "@/runtime/types";
import { PanelEmptyState, PanelToolbar, ToolPanel } from "@/components/panel";
import { AspectRatio } from "@/components/ui/aspect-ratio";
import { Badge } from "@/components/ui/badge";
import { Button } from "@/components/ui/button";

interface RuntimeViewportProps {
    state: RuntimeState;
    detail: string;
    session: RuntimeSession | null;
    projectOpen: boolean;
    onFrame(frame: HTMLIFrameElement | null): void;
    onPlay(): void;
    onRestart(): void;
}

const statePresentation: Record<
    RuntimeState,
    { label: string; variant: "outline" | "warning" | "success" | "destructive" }
> = {
    stopped: { label: "Stopped", variant: "outline" },
    starting: { label: "Starting", variant: "warning" },
    running: { label: "Running", variant: "success" },
    failed: { label: "Failed", variant: "destructive" },
};

export function RuntimeViewport({
    state,
    detail,
    session,
    projectOpen,
    onFrame,
    onPlay,
    onRestart,
}: RuntimeViewportProps) {
    const presentation = statePresentation[state];
    return (
        <ToolPanel>
            <PanelToolbar>
                <span>16:9</span>
                <Badge variant={presentation.variant} className="font-sans tracking-wide">
                    {presentation.label}
                </Badge>
            </PanelToolbar>
            <div className="relative flex min-h-0 flex-1 items-center justify-center overflow-hidden bg-[#080b10] p-2 [container-type:size]">
                <div className="w-[min(100cqw,calc(100cqh*16/9))] max-w-full">
                    <AspectRatio
                        ratio={16 / 9}
                        className="overflow-hidden rounded-md border border-border/70 bg-[#0a0e13] shadow-[0_12px_36px_rgb(0_0_0/0.28)]"
                    >
                        {session ? (
                            <iframe
                                key={session.channelId}
                                ref={onFrame}
                                className="block size-full border-0 bg-[#111722]"
                                title="fei project runtime"
                                allow="fullscreen"
                                src={session.source}
                            />
                        ) : (
                            <PanelEmptyState className="absolute inset-0 bg-[linear-gradient(45deg,rgb(255_255_255/0.015)_25%,transparent_25%)_0_0/16px_16px]">
                                <span className="grid size-10 place-items-center rounded-full border border-border bg-secondary text-primary shadow-inner">
                                    <Play size={18} fill="currentColor" />
                                </span>
                                <div className="grid gap-1">
                                    <strong className="text-xs text-[#b7bdc8]">
                                        {projectOpen ? "Project is stopped" : "No project open"}
                                    </strong>
                                    <span>
                                        {projectOpen
                                            ? "Start the isolated WebAssembly runtime."
                                            : "Open a project before starting the runtime."}
                                    </span>
                                </div>
                                <Button size="sm" disabled={!projectOpen} onClick={onPlay}>
                                    <Play size={13} fill="currentColor" />
                                    Start runtime
                                </Button>
                            </PanelEmptyState>
                        )}

                        {session && state === "starting" && (
                            <div className="absolute inset-0 grid place-content-center justify-items-center gap-2 bg-black/45 text-[10px] text-muted-foreground backdrop-blur-[1px]">
                                <LoaderCircle className="animate-spin text-primary" size={22} />
                                <strong className="text-[#d5dbe5]">Starting runtime…</strong>
                            </div>
                        )}

                        {session && state === "failed" && (
                            <div className="absolute inset-0 grid place-content-center justify-items-center gap-2 bg-[#130c10]/85 px-6 text-center text-[10px] text-muted-foreground backdrop-blur-sm">
                                <TriangleAlert className="text-destructive" size={23} />
                                <strong className="text-[#ffb4c0]">Runtime failed</strong>
                                <span className="max-w-72 truncate" title={detail}>{detail}</span>
                                <Button variant="outline" size="sm" onClick={onRestart}>
                                    <RotateCcw size={13} />
                                    Restart
                                </Button>
                            </div>
                        )}
                    </AspectRatio>
                </div>
            </div>
        </ToolPanel>
    );
}
