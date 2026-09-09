import { Popover as PopoverPrimitive } from "radix-ui";
import type { ComponentProps } from "react";
import { cn } from "@/lib/utils";

function Popover(props: ComponentProps<typeof PopoverPrimitive.Root>) {
    return <PopoverPrimitive.Root {...props} />;
}

function PopoverTrigger(props: ComponentProps<typeof PopoverPrimitive.Trigger>) {
    return <PopoverPrimitive.Trigger {...props} />;
}

function PopoverContent({
    className,
    align = "center",
    sideOffset = 4,
    ...props
}: ComponentProps<typeof PopoverPrimitive.Content>) {
    return (
        <PopoverPrimitive.Portal>
            <PopoverPrimitive.Content
                align={align}
                sideOffset={sideOffset}
                className={cn(
                    "z-[100] w-72 rounded-lg border border-border bg-popover p-1 text-popover-foreground shadow-[0_18px_48px_rgb(0_0_0/0.48)] outline-none data-[state=closed]:animate-out data-[state=open]:animate-in",
                    className,
                )}
                {...props}
            />
        </PopoverPrimitive.Portal>
    );
}

export { Popover, PopoverContent, PopoverTrigger };
