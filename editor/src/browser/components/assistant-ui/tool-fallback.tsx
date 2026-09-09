import type { ToolCallMessagePartProps } from "@assistant-ui/react";
import { AlertCircle, Check, ChevronDown, LoaderCircle, XCircle } from "lucide-react";
import { useEffect, useRef, useState } from "react";
import { Collapsible, CollapsibleContent, CollapsibleTrigger } from "@/components/ui/collapsible";
import { cn } from "@/lib/utils";

function formatValue(value: unknown): string {
    if (typeof value === "string") return value;
    try {
        return JSON.stringify(value, null, 2);
    } catch {
        return String(value);
    }
}

export function ToolFallback({
    toolName,
    args,
    result,
    isError,
}: ToolCallMessagePartProps) {
    const value = result && typeof result === "object" && "value" in result ? result.value : undefined;
    const image = value && typeof value === "object" && "mimeType" in value && "data" in value
        && value.mimeType === "image/png" && typeof value.data === "string" ? value.data : undefined;
    const progress = result && typeof result === "object" && "inProgress" in result && result.inProgress === true;
    const complete = result !== undefined && !progress;
    const failed = Boolean(isError);
    const [open, setOpen] = useState(!complete || failed);
    const previousCompleteRef = useRef(complete);
    const userControlledRef = useRef(false);

    useEffect(() => {
        const wasComplete = previousCompleteRef.current;
        if (!complete && wasComplete) {
            userControlledRef.current = false;
            setOpen(true);
        } else if (complete && !wasComplete && !userControlledRef.current) {
            setOpen(failed);
        } else if (failed && !userControlledRef.current) {
            setOpen(true);
        }
        previousCompleteRef.current = complete;
    }, [complete, failed]);

    const handleOpenChange = (nextOpen: boolean): void => {
        userControlledRef.current = true;
        setOpen(nextOpen);
    };

    const StatusIcon = !complete ? LoaderCircle : isError ? XCircle : Check;

    return (
        <Collapsible open={open} onOpenChange={handleOpenChange} className="aui-tool-fallback-root w-full">
            <CollapsibleTrigger className="group/tool-trigger m-0 flex w-full appearance-none items-center gap-2 rounded-md border-0 bg-transparent p-0 py-0.5 text-left text-[13px] leading-5 text-muted-foreground outline-none transition-colors hover:text-foreground focus-visible:ring-2 focus-visible:ring-ring/35">
                <StatusIcon
                    className={cn(
                        "size-3.5 shrink-0",
                        !complete && "animate-spin",
                        complete && !isError && "text-emerald-400",
                        isError && "text-destructive",
                    )}
                />
                <span className="min-w-0 truncate">
                    Used tool: <b className="font-mono font-medium text-foreground">{toolName}</b>
                </span>
                <span className="ml-auto text-xs">
                    {progress && "message" in result ? String(result.message) : !complete ? "Running" : isError ? "Failed" : "Completed"}
                </span>
                <ChevronDown className="size-3.5 shrink-0 transition-transform group-data-[state=closed]/tool-trigger:-rotate-90" />
            </CollapsibleTrigger>
            <CollapsibleContent className="overflow-hidden data-[state=closed]:animate-collapsible-up data-[state=open]:animate-collapsible-down">
                <div className="ml-1 mt-1 space-y-2 border-l border-border pl-4 pb-2">
                    <div>
                        <div className="mb-1 text-xs font-medium text-muted-foreground">Input</div>
                        <pre className="m-0 max-h-44 overflow-auto whitespace-pre-wrap rounded-lg border border-border/50 bg-muted/30 p-2.5 font-mono text-xs leading-relaxed text-foreground">
                            {formatValue(args)}
                        </pre>
                    </div>
                    {complete && (
                        <div>
                            <div className="mb-1 flex items-center gap-1 text-xs font-medium text-muted-foreground">
                                {isError && <AlertCircle className="size-3" />}
                                {isError ? "Error" : "Output"}
                            </div>
                            {image ? <img src={`data:image/png;base64,${image}`} alt="Native runtime capture" className="max-h-96 max-w-full rounded border border-border" /> : <pre className={cn(
                                "m-0 max-h-44 overflow-auto whitespace-pre-wrap rounded-lg border border-border/50 bg-muted/30 p-2.5 font-mono text-xs leading-relaxed text-foreground",
                                isError && "border-destructive/40 text-destructive",
                            )}>
                                {formatValue(result)}
                            </pre>}
                        </div>
                    )}
                </div>
            </CollapsibleContent>
        </Collapsible>
    );
}
