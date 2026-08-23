import type { ReasoningMessagePartProps } from "@assistant-ui/react";
import { Brain, ChevronDown } from "lucide-react";
import { useEffect, useRef, useState } from "react";
import { Collapsible, CollapsibleContent, CollapsibleTrigger } from "@/components/ui/collapsible";

export function Reasoning({ text, status }: ReasoningMessagePartProps) {
    const running = status.type === "running";
    const [open, setOpen] = useState(running);
    const previousRunningRef = useRef(running);
    const userControlledRef = useRef(false);

    useEffect(() => {
        const wasRunning = previousRunningRef.current;
        if (running && !wasRunning) {
            userControlledRef.current = false;
            setOpen(true);
        } else if (!running && wasRunning && !userControlledRef.current) {
            setOpen(false);
        }
        previousRunningRef.current = running;
    }, [running]);

    const handleOpenChange = (nextOpen: boolean): void => {
        userControlledRef.current = true;
        setOpen(nextOpen);
    };

    return (
        <Collapsible open={open} onOpenChange={handleOpenChange} className="aui-reasoning-root mb-2 w-full">
            <CollapsibleTrigger className="group/reasoning-trigger m-0 flex w-fit appearance-none items-center gap-2 rounded-md border-0 bg-transparent p-0 py-0.5 text-[13px] leading-5 font-medium text-muted-foreground outline-none transition-colors hover:text-foreground focus-visible:ring-2 focus-visible:ring-ring/35">
                <Brain className="size-3.5" />
                <span>{running ? "Thinking…" : "Reasoning"}</span>
                <ChevronDown className="size-3.5 transition-transform group-data-[state=closed]/reasoning-trigger:-rotate-90" />
            </CollapsibleTrigger>
            <CollapsibleContent className="data-[state=closed]:animate-collapsible-up data-[state=open]:animate-collapsible-down overflow-hidden">
                <div className="mt-1 border-l border-border pl-4 text-[13px] leading-relaxed text-muted-foreground">
                    {text}
                </div>
            </CollapsibleContent>
        </Collapsible>
    );
}
