import { Check } from "lucide-react";
import { Checkbox as CheckboxPrimitive } from "radix-ui";
import type { ComponentProps } from "react";
import { cn } from "@/lib/utils";

function Checkbox({ className, ...props }: ComponentProps<typeof CheckboxPrimitive.Root>) {
    return (
        <CheckboxPrimitive.Root
            className={cn(
                "peer size-3.5 shrink-0 rounded-[3px] border border-input bg-[#090d12] text-primary outline-none transition-[border-color,box-shadow,background-color] data-[state=checked]:border-primary data-[state=checked]:bg-primary data-[state=checked]:text-primary-foreground focus-visible:ring-2 focus-visible:ring-ring/35 disabled:cursor-not-allowed disabled:opacity-50",
                className,
            )}
            {...props}
        >
            <CheckboxPrimitive.Indicator className="grid size-full place-items-center">
                <Check className="size-3" strokeWidth={3} />
            </CheckboxPrimitive.Indicator>
        </CheckboxPrimitive.Root>
    );
}

export { Checkbox };
