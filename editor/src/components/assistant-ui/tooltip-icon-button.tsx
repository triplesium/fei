import type { ComponentProps } from "react";
import { Button } from "@/components/ui/button";
import { Tooltip, TooltipContent, TooltipTrigger } from "@/components/ui/tooltip";

type TooltipIconButtonProps = ComponentProps<typeof Button> & {
    tooltip: string;
    side?: ComponentProps<typeof TooltipContent>["side"];
};

export function TooltipIconButton({
    tooltip,
    side = "bottom",
    ...props
}: TooltipIconButtonProps) {
    return (
        <Tooltip>
            <TooltipTrigger asChild>
                <Button variant="ghost" size="icon" {...props} />
            </TooltipTrigger>
            <TooltipContent side={side}>{tooltip}</TooltipContent>
        </Tooltip>
    );
}
