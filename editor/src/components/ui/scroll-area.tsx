import { ScrollArea as ScrollAreaPrimitive } from "radix-ui";
import type { ComponentPropsWithoutRef, Ref } from "react";
import { cn } from "@/lib/utils";

interface ScrollAreaProps extends ComponentPropsWithoutRef<typeof ScrollAreaPrimitive.Root> {
    viewportRef?: Ref<HTMLDivElement>;
}

function ScrollArea({ className, children, viewportRef, ...props }: ScrollAreaProps) {
    return (
        <ScrollAreaPrimitive.Root
            className={cn("relative overflow-hidden", className)}
            {...props}
        >
            <ScrollAreaPrimitive.Viewport ref={viewportRef} className="size-full rounded-[inherit]">
                {children}
            </ScrollAreaPrimitive.Viewport>
            <ScrollBar />
            <ScrollAreaPrimitive.Corner />
        </ScrollAreaPrimitive.Root>
    );
}

function ScrollBar({
    className,
    orientation = "vertical",
    ...props
}: ComponentPropsWithoutRef<typeof ScrollAreaPrimitive.ScrollAreaScrollbar>) {
    return (
        <ScrollAreaPrimitive.ScrollAreaScrollbar
            orientation={orientation}
            className={cn(
                "flex touch-none select-none p-px transition-colors",
                orientation === "vertical" && "h-full w-2.5 border-l border-l-transparent",
                orientation === "horizontal" && "h-2.5 flex-col border-t border-t-transparent",
                className,
            )}
            {...props}
        >
            <ScrollAreaPrimitive.ScrollAreaThumb className="relative flex-1 rounded-full bg-border" />
        </ScrollAreaPrimitive.ScrollAreaScrollbar>
    );
}

export { ScrollArea, ScrollBar };
