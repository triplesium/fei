import { ChevronDown, Wrench } from "lucide-react";
import { useEffect, useRef, useState, type PropsWithChildren } from "react";
import { Collapsible, CollapsibleContent, CollapsibleTrigger } from "@/components/ui/collapsible";

interface ToolGroupProps extends PropsWithChildren {
    active: boolean;
    failed: boolean;
}

export function ToolGroup({ children, active, failed }: ToolGroupProps) {
    const [open, setOpen] = useState(active || failed);
    const previousActiveRef = useRef(active);
    const userControlledRef = useRef(false);

    useEffect(() => {
        const wasActive = previousActiveRef.current;
        if (active && !wasActive) {
            userControlledRef.current = false;
            setOpen(true);
        } else if (!active && wasActive && !userControlledRef.current) {
            setOpen(failed);
        } else if (failed && !userControlledRef.current) {
            setOpen(true);
        }
        previousActiveRef.current = active;
    }, [active, failed]);

    const handleOpenChange = (nextOpen: boolean): void => {
        userControlledRef.current = true;
        setOpen(nextOpen);
    };

    return (
        <Collapsible open={open} onOpenChange={handleOpenChange} className="aui-tool-group-root mb-2 w-full">
            <CollapsibleTrigger className="group/tool-group-trigger m-0 flex w-fit appearance-none items-center gap-2 rounded-md border-0 bg-transparent p-0 py-0.5 text-[13px] leading-5 font-medium text-muted-foreground outline-none transition-colors hover:text-foreground focus-visible:ring-2 focus-visible:ring-ring/35">
                <Wrench className="size-3.5" />
                <span>Tools</span>
                <ChevronDown className="size-3.5 transition-transform group-data-[state=closed]/tool-group-trigger:-rotate-90" />
            </CollapsibleTrigger>
            <CollapsibleContent className="mt-1 space-y-1 overflow-hidden data-[state=closed]:animate-collapsible-up data-[state=open]:animate-collapsible-down">
                {children}
            </CollapsibleContent>
        </Collapsible>
    );
}
