import { DropdownMenu as DropdownMenuPrimitive } from "radix-ui";
import type { ComponentProps } from "react";
import { cn } from "@/lib/utils";

function DropdownMenu(props: ComponentProps<typeof DropdownMenuPrimitive.Root>) {
    return <DropdownMenuPrimitive.Root {...props} />;
}

function DropdownMenuTrigger(props: ComponentProps<typeof DropdownMenuPrimitive.Trigger>) {
    return <DropdownMenuPrimitive.Trigger {...props} />;
}

function DropdownMenuContent({
    className,
    sideOffset = 5,
    ...props
}: ComponentProps<typeof DropdownMenuPrimitive.Content>) {
    return (
        <DropdownMenuPrimitive.Portal>
            <DropdownMenuPrimitive.Content
                sideOffset={sideOffset}
                className={cn(
                    "z-[100] min-w-52 overflow-hidden rounded-lg border border-border bg-popover p-1 text-popover-foreground shadow-[0_14px_40px_rgb(0_0_0/0.46)] outline-none",
                    className,
                )}
                {...props}
            />
        </DropdownMenuPrimitive.Portal>
    );
}

function DropdownMenuItem({ className, ...props }: ComponentProps<typeof DropdownMenuPrimitive.Item>) {
    return (
        <DropdownMenuPrimitive.Item
            className={cn(
                "relative flex min-h-7 cursor-default select-none items-center gap-2 rounded-md px-2 text-[12px] outline-none data-[disabled]:pointer-events-none data-[highlighted]:bg-accent data-[highlighted]:text-accent-foreground data-[disabled]:opacity-40",
                className,
            )}
            {...props}
        />
    );
}

function DropdownMenuSeparator({ className, ...props }: ComponentProps<typeof DropdownMenuPrimitive.Separator>) {
    return (
        <DropdownMenuPrimitive.Separator
            className={cn("-mx-1 my-1 h-px bg-border", className)}
            {...props}
        />
    );
}

function DropdownMenuShortcut({ className, ...props }: ComponentProps<"span">) {
    return (
        <span
            className={cn("ml-auto pl-5 text-[11px] tracking-wide text-muted-foreground", className)}
            {...props}
        />
    );
}

export {
    DropdownMenu,
    DropdownMenuContent,
    DropdownMenuItem,
    DropdownMenuSeparator,
    DropdownMenuShortcut,
    DropdownMenuTrigger,
};
