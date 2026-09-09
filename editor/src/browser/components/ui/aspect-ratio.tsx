import { AspectRatio as AspectRatioPrimitive } from "radix-ui";
import type { ComponentPropsWithoutRef } from "react";
import { cn } from "@/lib/utils";

function AspectRatio({ className, ...props }: ComponentPropsWithoutRef<typeof AspectRatioPrimitive.Root>) {
    return <AspectRatioPrimitive.Root className={cn("relative", className)} {...props} />;
}

export { AspectRatio };
