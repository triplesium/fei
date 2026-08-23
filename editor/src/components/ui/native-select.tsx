import { ChevronDown } from "lucide-react";
import type { ComponentProps } from "react";
import { cn } from "@/lib/utils";

function NativeSelect({ className, children, ...props }: ComponentProps<"select">) {
    return (
        <span className="relative block">
            <select
                className={cn(
                    "h-9 w-full appearance-none rounded-md border border-input bg-[#090d12] px-3 pr-8 text-[11px] text-foreground outline-none transition-[border-color,box-shadow] focus:border-primary/60 focus:ring-2 focus:ring-ring/10 disabled:cursor-not-allowed disabled:opacity-50",
                    className,
                )}
                {...props}
            >
                {children}
            </select>
            <ChevronDown className="pointer-events-none absolute right-2.5 top-1/2 size-3 -translate-y-1/2 text-muted-foreground" />
        </span>
    );
}

export { NativeSelect };
