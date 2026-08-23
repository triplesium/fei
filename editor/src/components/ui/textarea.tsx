import type { ComponentProps } from "react";
import { cn } from "@/lib/utils";

function Textarea({ className, ...props }: ComponentProps<"textarea">) {
    return (
        <textarea
            className={cn(
                "min-h-20 w-full resize-y rounded-md border border-input bg-[#090d12] px-3 py-2 font-mono text-[10px] leading-5 text-foreground outline-none transition-[border-color,box-shadow] placeholder:text-muted-foreground focus:border-primary/60 focus:ring-2 focus:ring-ring/10 disabled:cursor-not-allowed disabled:opacity-50",
                className,
            )}
            {...props}
        />
    );
}

export { Textarea };
