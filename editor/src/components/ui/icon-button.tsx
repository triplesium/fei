import type { ReactNode } from "react";
import { Button } from "@/components/ui/button";
import { Tooltip, TooltipContent, TooltipTrigger } from "@/components/ui/tooltip";
import { cn } from "@/lib/utils";

interface IconButtonProps {
    id?: string;
    label: string;
    disabled?: boolean;
    danger?: boolean;
    accent?: "play";
    className?: string;
    onClick?(): void;
    children: ReactNode;
}

export function IconButton({
    id,
    label,
    disabled,
    danger = false,
    accent,
    className,
    onClick,
    children,
}: IconButtonProps) {
    return (
        <Tooltip>
            <TooltipTrigger asChild>
                <Button
                    id={id}
                    variant="ghost"
                    size="icon"
                    className={cn(
                        danger && "text-destructive hover:bg-destructive/10 hover:text-[#ff9aaa]",
                        accent === "play" && "text-[#8cddb5]",
                        className,
                    )}
                    type="button"
                    aria-label={label}
                    disabled={disabled}
                    onClick={onClick}
                >
                    {children}
                </Button>
            </TooltipTrigger>
            <TooltipContent>{label}</TooltipContent>
        </Tooltip>
    );
}
