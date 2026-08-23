import { cva, type VariantProps } from "class-variance-authority";
import type { HTMLAttributes } from "react";
import { cn } from "@/lib/utils";

const badgeVariants = cva(
    "inline-flex max-w-full items-center rounded border px-1.5 py-0.5 font-mono text-[8px] leading-none",
    {
        variants: {
            variant: {
                default: "border-transparent bg-primary/15 text-primary",
                secondary: "border-border bg-black/20 text-[#aeb9c8]",
                outline: "border-border bg-transparent text-muted-foreground",
                success: "border-emerald-400/20 bg-emerald-400/10 text-emerald-300",
                warning: "border-amber-400/20 bg-amber-400/10 text-amber-300",
                destructive: "border-destructive/25 bg-destructive/10 text-[#ff9aaa]",
            },
        },
        defaultVariants: {
            variant: "secondary",
        },
    },
);

interface BadgeProps extends HTMLAttributes<HTMLSpanElement>, VariantProps<typeof badgeVariants> {}

function Badge({ className, variant, ...props }: BadgeProps) {
    return <span className={cn(badgeVariants({ variant }), className)} {...props} />;
}

export { Badge, badgeVariants };
