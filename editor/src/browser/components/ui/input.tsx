import type { ComponentProps } from "react";
import { cn } from "@/lib/utils";

function Input({ className, type, ...props }: ComponentProps<"input">) {
    return (
        <input
            type={type}
            className={cn(
                "h-9 w-full rounded-md border border-input bg-[#090d12] px-3 text-[11px] text-foreground outline-none transition-[border-color,box-shadow] placeholder:text-muted-foreground focus:border-primary/60 focus:ring-2 focus:ring-ring/10 disabled:cursor-not-allowed disabled:opacity-50",
                className,
            )}
            {...props}
        />
    );
}

export { Input };
