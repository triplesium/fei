import { Slot } from "radix-ui";
import { cva, type VariantProps } from "class-variance-authority";
import type { ButtonHTMLAttributes } from "react";
import { cn } from "@/lib/utils";

const buttonVariants = cva(
    "m-0 inline-flex shrink-0 appearance-none items-center justify-center gap-1.5 whitespace-nowrap rounded-md border-0 text-[10px] font-semibold outline-none transition-colors disabled:pointer-events-none disabled:opacity-50 focus-visible:ring-2 focus-visible:ring-ring/35 [&_svg]:pointer-events-none [&_svg]:shrink-0",
    {
        variants: {
            variant: {
                default: "bg-primary text-primary-foreground hover:bg-primary/90",
                secondary: "bg-secondary text-secondary-foreground hover:bg-muted",
                ghost: "bg-transparent text-muted-foreground hover:bg-muted hover:text-foreground data-[state=open]:bg-muted data-[state=open]:text-foreground",
                outline: "border border-border bg-transparent text-foreground hover:bg-muted",
                destructive: "bg-destructive text-white hover:bg-destructive/90",
            },
            size: {
                default: "h-8 px-3",
                sm: "h-7 px-2",
                icon: "size-7",
            },
        },
        defaultVariants: {
            variant: "default",
            size: "default",
        },
    },
);

interface ButtonProps
    extends ButtonHTMLAttributes<HTMLButtonElement>,
        VariantProps<typeof buttonVariants> {
    asChild?: boolean;
}

function Button({ className, variant, size, asChild = false, ...props }: ButtonProps) {
    const Component = asChild ? Slot.Root : "button";
    return <Component className={cn(buttonVariants({ variant, size }), className)} {...props} />;
}

export { Button, buttonVariants };
