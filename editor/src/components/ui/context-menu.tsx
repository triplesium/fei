import { ContextMenu as ContextMenuPrimitive } from "radix-ui";
import type { ComponentProps } from "react";
import { cn } from "@/lib/utils";

function ContextMenu(props: ComponentProps<typeof ContextMenuPrimitive.Root>) {
    return <ContextMenuPrimitive.Root {...props} />;
}

function ContextMenuTrigger(props: ComponentProps<typeof ContextMenuPrimitive.Trigger>) {
    return <ContextMenuPrimitive.Trigger {...props} />;
}

function ContextMenuContent({
    className,
    ...props
}: ComponentProps<typeof ContextMenuPrimitive.Content>) {
    return (
        <ContextMenuPrimitive.Portal>
            <ContextMenuPrimitive.Content
                className={cn(
                    "z-[100] min-w-52 overflow-hidden rounded-lg border border-border bg-popover p-1 text-popover-foreground shadow-[0_14px_40px_rgb(0_0_0/0.46)] outline-none",
                    className,
                )}
                {...props}
            />
        </ContextMenuPrimitive.Portal>
    );
}

function ContextMenuItem({
    className,
    ...props
}: ComponentProps<typeof ContextMenuPrimitive.Item>) {
    return (
        <ContextMenuPrimitive.Item
            className={cn(
                "relative flex min-h-7 cursor-default select-none items-center gap-2 rounded-md px-2 text-[12px] outline-none data-[disabled]:pointer-events-none data-[highlighted]:bg-accent data-[highlighted]:text-accent-foreground data-[disabled]:opacity-40 [&_svg]:size-3.5 [&_svg]:shrink-0",
                className,
            )}
            {...props}
        />
    );
}

function ContextMenuSeparator({
    className,
    ...props
}: ComponentProps<typeof ContextMenuPrimitive.Separator>) {
    return (
        <ContextMenuPrimitive.Separator
            className={cn("-mx-1 my-1 h-px bg-border", className)}
            {...props}
        />
    );
}

export {
    ContextMenu,
    ContextMenuContent,
    ContextMenuItem,
    ContextMenuSeparator,
    ContextMenuTrigger,
};
