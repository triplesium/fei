import { Bot } from "lucide-react";
import {
    AgentModelSettings,
    type AgentModelSettingsProps,
} from "@/components/agent-model-dialog";
import {
    Dialog,
    DialogContent,
    DialogDescription,
    DialogHeader,
    DialogTitle,
} from "@/components/ui/dialog";

interface SettingsDialogProps extends AgentModelSettingsProps {
    open: boolean;
    onOpenChange(open: boolean): void;
}

export function SettingsDialog({
    open,
    onOpenChange,
    ...modelSettingsProps
}: SettingsDialogProps) {
    return (
        <Dialog open={open} onOpenChange={onOpenChange}>
            <DialogContent className="grid h-[min(620px,calc(100vh-30px))] max-h-none w-[min(820px,calc(100vw-30px))] max-w-none grid-cols-[150px_minmax(0,1fr)] grid-rows-[auto_minmax(0,1fr)] gap-0 overflow-hidden p-0 max-[680px]:grid-cols-1 max-[680px]:grid-rows-[auto_auto_minmax(0,1fr)]">
                <DialogHeader className="col-span-2 border-b border-border px-5 py-4 max-[680px]:col-span-1">
                    <DialogTitle>Settings</DialogTitle>
                    <DialogDescription>
                        Manage OpenAI-compatible providers and models. Credentials stay in the local Host.
                    </DialogDescription>
                </DialogHeader>

                <nav
                    className="min-h-0 border-r border-border bg-black/10 p-2 max-[680px]:border-b max-[680px]:border-r-0"
                    aria-label="Settings categories"
                >
                    <div
                        aria-current="page"
                        className="flex h-9 items-center gap-2 rounded-md bg-muted px-2.5 text-[11px] text-foreground"
                    >
                        <Bot className="size-3.5" />
                        <span>Models</span>
                    </div>
                </nav>

                <div className="min-h-0 overflow-y-auto px-6 py-5 max-[560px]:px-4">
                    <AgentModelSettings {...modelSettingsProps} />
                </div>
            </DialogContent>
        </Dialog>
    );
}
