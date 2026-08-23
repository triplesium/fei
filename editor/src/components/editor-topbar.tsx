import { CircleStop, Play, RotateCcw } from "lucide-react";
import type { RuntimeState } from "@/runtime/types";
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

export function EditorTopbar({
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
    return (
        <header className="grid h-[46px] shrink-0 grid-cols-[1fr_auto_1fr] items-center gap-2.5 border-b border-[#0d0d0d] bg-[#171717] px-2.5 shadow-[inset_0_-1px_rgb(255_255_255/0.025)]">
            <div className="flex min-w-0 items-center">
                <nav className="flex items-center gap-px max-[900px]:hidden" aria-label="Application menu">
                    <DropdownMenu>
                        <DropdownMenuTrigger asChild>
                            <Button variant="ghost" size="sm" className="h-7 px-2 text-[13px]">File</Button>
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
                            <Button variant="ghost" size="sm" className="h-7 px-2 text-[13px]">View</Button>
                        </DropdownMenuTrigger>
                        <DropdownMenuContent align="start">
                            <DropdownMenuItem onSelect={onResetWorkbench}>Reset Workbench Layout</DropdownMenuItem>
                        </DropdownMenuContent>
                    </DropdownMenu>
                </nav>
            </div>

            <div className="flex items-center gap-1 rounded border border-[#303030] bg-[#202020] px-1 py-0.5 shadow-inner max-[900px]:justify-self-center" aria-label="Runtime controls">
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

        </header>
    );
}
