import { Command as CommandPrimitive } from "cmdk";
import { Search } from "lucide-react";
import type { ComponentProps } from "react";
import { cn } from "@/lib/utils";

function Command({ className, ...props }: ComponentProps<typeof CommandPrimitive>) {
    return (
        <CommandPrimitive
            className={cn("flex h-full w-full flex-col overflow-hidden rounded-md bg-popover", className)}
            {...props}
        />
    );
}

function CommandInput({ className, ...props }: ComponentProps<typeof CommandPrimitive.Input>) {
    return (
        <div className="flex h-9 items-center gap-2 border-b border-border px-2.5" cmdk-input-wrapper="">
            <Search className="size-3.5 shrink-0 text-muted-foreground" />
            <CommandPrimitive.Input
                className={cn(
                    "h-full min-w-0 flex-1 border-0 bg-transparent text-[10px] text-foreground outline-none placeholder:text-muted-foreground disabled:cursor-not-allowed disabled:opacity-50",
                    className,
                )}
                {...props}
            />
        </div>
    );
}

function CommandList({ className, ...props }: ComponentProps<typeof CommandPrimitive.List>) {
    return (
        <CommandPrimitive.List
            className={cn("max-h-[310px] overflow-x-hidden overflow-y-auto p-1", className)}
            {...props}
        />
    );
}

function CommandEmpty({ className, ...props }: ComponentProps<typeof CommandPrimitive.Empty>) {
    return (
        <CommandPrimitive.Empty
            className={cn("py-8 text-center text-[10px] text-muted-foreground", className)}
            {...props}
        />
    );
}

function CommandGroup({ className, ...props }: ComponentProps<typeof CommandPrimitive.Group>) {
    return (
        <CommandPrimitive.Group
            className={cn(
                "overflow-hidden p-1 text-foreground [&_[cmdk-group-heading]]:px-1.5 [&_[cmdk-group-heading]]:py-1.5 [&_[cmdk-group-heading]]:text-[8px] [&_[cmdk-group-heading]]:font-bold [&_[cmdk-group-heading]]:uppercase [&_[cmdk-group-heading]]:tracking-[0.07em] [&_[cmdk-group-heading]]:text-muted-foreground",
                className,
            )}
            {...props}
        />
    );
}

function CommandItem({ className, ...props }: ComponentProps<typeof CommandPrimitive.Item>) {
    return (
        <CommandPrimitive.Item
            className={cn(
                "relative flex min-h-10 cursor-default select-none items-center gap-2 rounded-md px-2 py-1.5 text-[10px] outline-none data-[disabled=true]:pointer-events-none data-[selected=true]:bg-accent data-[selected=true]:text-accent-foreground data-[disabled=true]:opacity-50",
                className,
            )}
            {...props}
        />
    );
}

function CommandSeparator({ className, ...props }: ComponentProps<typeof CommandPrimitive.Separator>) {
    return (
        <CommandPrimitive.Separator
            className={cn("-mx-1 my-1 h-px bg-border", className)}
            {...props}
        />
    );
}

export {
    Command,
    CommandEmpty,
    CommandGroup,
    CommandInput,
    CommandItem,
    CommandList,
    CommandSeparator,
};
