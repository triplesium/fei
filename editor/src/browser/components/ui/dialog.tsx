import { Dialog as DialogPrimitive } from "radix-ui";
import { X } from "lucide-react";
import type { ComponentProps } from "react";
import { cn } from "@/lib/utils";

function Dialog(props: ComponentProps<typeof DialogPrimitive.Root>) {
    return <DialogPrimitive.Root {...props} />;
}

function DialogTrigger(props: ComponentProps<typeof DialogPrimitive.Trigger>) {
    return <DialogPrimitive.Trigger {...props} />;
}

function DialogClose(props: ComponentProps<typeof DialogPrimitive.Close>) {
    return <DialogPrimitive.Close {...props} />;
}

function DialogPortal(props: ComponentProps<typeof DialogPrimitive.Portal>) {
    return <DialogPrimitive.Portal {...props} />;
}

function DialogOverlay({ className, ...props }: ComponentProps<typeof DialogPrimitive.Overlay>) {
    return (
        <DialogPrimitive.Overlay
            className={cn(
                "fixed inset-0 z-[110] bg-black/70 backdrop-blur-[3px]",
                className,
            )}
            {...props}
        />
    );
}

function DialogContent({
    className,
    children,
    showCloseButton = true,
    ...props
}: ComponentProps<typeof DialogPrimitive.Content> & { showCloseButton?: boolean }) {
    return (
        <DialogPortal>
            <DialogOverlay />
            <DialogPrimitive.Content
                className={cn(
                    "fixed left-1/2 top-1/2 z-[111] grid w-[min(540px,calc(100vw-30px))] max-h-[min(760px,calc(100vh-30px))] -translate-x-1/2 -translate-y-1/2 gap-4 overflow-y-auto rounded-xl border border-border bg-[#121821] p-5 text-foreground shadow-[0_24px_80px_rgb(0_0_0/0.55)] outline-none",
                    className,
                )}
                {...props}
            >
                {children}
                {showCloseButton && (
                    <DialogPrimitive.Close
                        className="absolute right-3 top-3 m-0 grid size-7 appearance-none place-items-center rounded-md border-0 bg-transparent p-0 text-muted-foreground outline-none transition-colors hover:bg-muted hover:text-foreground focus-visible:ring-2 focus-visible:ring-ring/35"
                        aria-label="Close"
                    >
                        <X className="size-3.5" />
                    </DialogPrimitive.Close>
                )}
            </DialogPrimitive.Content>
        </DialogPortal>
    );
}

function DialogHeader({ className, ...props }: ComponentProps<"div">) {
    return <div className={cn("grid gap-1 pr-8", className)} {...props} />;
}

function DialogFooter({ className, ...props }: ComponentProps<"div">) {
    return <div className={cn("flex flex-wrap justify-end gap-2 pt-1", className)} {...props} />;
}

function DialogTitle({ className, ...props }: ComponentProps<typeof DialogPrimitive.Title>) {
    return <DialogPrimitive.Title className={cn("m-0 text-sm font-semibold", className)} {...props} />;
}

function DialogDescription({ className, ...props }: ComponentProps<typeof DialogPrimitive.Description>) {
    return (
        <DialogPrimitive.Description
            className={cn("m-0 text-[11px] leading-5 text-muted-foreground", className)}
            {...props}
        />
    );
}

export {
    Dialog,
    DialogClose,
    DialogContent,
    DialogDescription,
    DialogFooter,
    DialogHeader,
    DialogOverlay,
    DialogPortal,
    DialogTitle,
    DialogTrigger,
};
