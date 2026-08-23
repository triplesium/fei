import { CircleStop, FolderOpen, Play, RotateCcw, Save } from "lucide-react";
import type { RuntimeState } from "@/runtime/types";
import { Badge } from "@/components/ui/badge";
import { Button } from "@/components/ui/button";
import {
    DropdownMenu,
    DropdownMenuContent,
    DropdownMenuItem,
    DropdownMenuSeparator,
    DropdownMenuShortcut,
    DropdownMenuTrigger,
} from "@/components/ui/dropdown-menu";
import { IconButton } from "@/components/ui/icon-button";

interface EditorTopbarProps {
    projectName: string;
    openFolderLabel: string;
    projectOpen: boolean;
    canSave: boolean;
    runtimeState: RuntimeState;
    onOpenProject(): void;
    onSave(): void;
    onOpenProjectSettings(): void;
    onOpenSettings(): void;
    onResetWorkbench(): void;
    onPlay(): void;
    onStop(): void;
    onRestart(): void;
}

const runtimePresentation: Record<
    RuntimeState,
    { label: string; variant: "outline" | "warning" | "success" | "destructive"; dot: string }
> = {
    stopped: { label: "Stopped", variant: "outline", dot: "bg-muted-foreground" },
    starting: { label: "Starting", variant: "warning", dot: "bg-amber-300" },
    running: { label: "Running", variant: "success", dot: "bg-emerald-300" },
    failed: { label: "Failed", variant: "destructive", dot: "bg-destructive" },
};

export function EditorTopbar({
    projectName,
    openFolderLabel,
    projectOpen,
    canSave,
    runtimeState,
    onOpenProject,
    onSave,
    onOpenProjectSettings,
    onOpenSettings,
    onResetWorkbench,
    onPlay,
    onStop,
    onRestart,
}: EditorTopbarProps) {
    const runtime = runtimePresentation[runtimeState];
    return (
        <header className="grid h-[42px] shrink-0 grid-cols-[auto_auto_minmax(100px,1fr)_auto_minmax(220px,1fr)] items-center gap-2.5 border-b border-border bg-[#1b1e23] px-2 max-[900px]:grid-cols-[auto_1fr_auto]">
            <div className="flex items-center gap-2">
                <span className="grid size-[25px] shrink-0 place-items-center rounded-md border border-primary/40 bg-primary/10 text-[11px] font-extrabold text-primary shadow-[inset_0_0_16px_rgb(109_158_255/0.08)]">
                    F
                </span>
                <span className="text-[11px] font-extrabold tracking-[0.13em] max-[900px]:hidden">ENTISIUM</span>
            </div>

            <nav className="flex items-center gap-px max-[900px]:hidden" aria-label="Application menu">
                <DropdownMenu>
                    <DropdownMenuTrigger asChild>
                        <Button variant="ghost" size="sm" className="h-7 px-2 text-[11px]">File</Button>
                    </DropdownMenuTrigger>
                    <DropdownMenuContent align="start">
                        <DropdownMenuItem onSelect={onOpenProject}>
                            Open Folder<DropdownMenuShortcut>Ctrl+O</DropdownMenuShortcut>
                        </DropdownMenuItem>
                        <DropdownMenuItem disabled={!canSave} onSelect={onSave}>
                            Save<DropdownMenuShortcut>Ctrl+S</DropdownMenuShortcut>
                        </DropdownMenuItem>
                        <DropdownMenuSeparator />
                        <DropdownMenuItem onSelect={onOpenSettings}>
                            Settings…<DropdownMenuShortcut>Ctrl+,</DropdownMenuShortcut>
                        </DropdownMenuItem>
                        <DropdownMenuItem disabled={!projectOpen} onSelect={onOpenProjectSettings}>
                            Project Settings…
                        </DropdownMenuItem>
                    </DropdownMenuContent>
                </DropdownMenu>
                <DropdownMenu>
                    <DropdownMenuTrigger asChild>
                        <Button variant="ghost" size="sm" className="h-7 px-2 text-[11px]">View</Button>
                    </DropdownMenuTrigger>
                    <DropdownMenuContent align="start">
                        <DropdownMenuItem onSelect={onResetWorkbench}>Reset Workbench Layout</DropdownMenuItem>
                    </DropdownMenuContent>
                </DropdownMenu>
            </nav>

            <div className="min-w-0 truncate text-center text-[10px] text-muted-foreground max-[900px]:hidden" title={projectName}>
                {projectName}
            </div>

            <div className="flex items-center gap-1 rounded-md border border-border bg-[#17191e] px-1 py-0.5 max-[900px]:justify-self-center" aria-label="Runtime controls">
                <IconButton
                    id="play"
                    label="Play"
                    accent="play"
                    disabled={!projectOpen || runtimeState === "starting" || runtimeState === "running"}
                    onClick={onPlay}
                >
                    <Play size={14} fill="currentColor" />
                </IconButton>
                <IconButton label="Stop" danger disabled={runtimeState === "stopped"} onClick={onStop}>
                    <CircleStop size={15} />
                </IconButton>
                <IconButton
                    label="Restart"
                    disabled={runtimeState !== "running" && runtimeState !== "failed"}
                    onClick={onRestart}
                >
                    <RotateCcw size={14} />
                </IconButton>
            </div>

            <div className="flex items-center justify-end gap-1.5" aria-label="Project controls">
                <Button
                    id="open-folder"
                    variant="secondary"
                    size="sm"
                    className="max-w-[210px] truncate text-[11px] max-[900px]:max-w-[130px]"
                    type="button"
                    onClick={onOpenProject}
                >
                    <FolderOpen size={14} />
                    <span className="truncate">{openFolderLabel}</span>
                </Button>
                <IconButton label="Save (Ctrl+S)" disabled={!canSave} onClick={onSave}>
                    <Save size={14} />
                </IconButton>
                <Badge variant={runtime.variant} className="ml-1 min-w-[68px] justify-center gap-1.5 font-sans text-[9px] max-[900px]:hidden">
                    <span className={`size-1.5 rounded-full ${runtime.dot}`} />
                    {runtime.label}
                </Badge>
            </div>
        </header>
    );
}
