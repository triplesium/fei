import { Separator as SeparatorPrimitive } from "radix-ui";
import type { ComponentPropsWithoutRef } from "react";
import { cn } from "@/lib/utils";

function Separator({
    className,
    orientation = "horizontal",
    decorative = true,
    ...props
}: ComponentPropsWithoutRef<typeof SeparatorPrimitive.Root>) {
    return (
        <SeparatorPrimitive.Root
            decorative={decorative}
            orientation={orientation}
            className={cn(
                "shrink-0 bg-border/70",
                orientation === "horizontal" ? "h-px w-full" : "h-full w-px",
                className,
            )}
            {...props}
        />
    );
}

export { Separator };
