import type { HTMLAttributes, PropsWithChildren, ReactNode } from "react";
import { cn } from "@/lib/utils";

interface PanelHeaderProps {
    title: string;
    detail?: ReactNode;
    actions?: ReactNode;
}

export function PanelHeader({ title, detail, actions }: PanelHeaderProps) {
    return (
        <header className="flex h-[34px] shrink-0 items-center justify-between gap-2 border-b border-[#171717] bg-[#202020] px-2.5">
            <div className="flex min-w-0 items-center gap-2 text-[12px] font-bold tracking-[0.06em] text-[#b7bdc8]">
                <span className="truncate">{title}</span>
                {detail && <span className="text-[11px] font-medium tracking-normal text-muted-foreground">{detail}</span>}
            </div>
            {actions && <div className="flex items-center gap-0.5">{actions}</div>}
        </header>
    );
}

export function ToolPanel({ children, className = "" }: PropsWithChildren<{ className?: string }>) {
    return (
        <section className={cn("flex size-full min-h-0 min-w-0 flex-col overflow-hidden bg-card", className)}>
            {children}
        </section>
    );
}

export function PanelToolbar({ className, ...props }: HTMLAttributes<HTMLDivElement>) {
    return (
        <div
            className={cn(
                "flex h-[30px] shrink-0 items-center justify-between gap-2 border-b border-[#191919] bg-[#2b2b2b] py-0 pr-1.5 pl-2.5 text-[12px] text-muted-foreground",
                className,
            )}
            {...props}
        />
    );
}

export function PanelStatus({ className, ...props }: HTMLAttributes<HTMLElement>) {
    return (
        <footer
            className={cn(
                "flex h-[24px] shrink-0 items-center justify-between gap-3 border-t border-[#171717] bg-[#1d1d1d] px-2.5 text-[11px] text-muted-foreground",
                className,
            )}
            {...props}
        />
    );
}

export function PanelSection({ className, ...props }: HTMLAttributes<HTMLDivElement>) {
    return <div className={cn("px-3 py-3", className)} {...props} />;
}

export function PanelSectionTitle({ className, ...props }: HTMLAttributes<HTMLSpanElement>) {
    return (
        <span
            className={cn("text-[11px] font-bold tracking-[0.08em] text-muted-foreground", className)}
            {...props}
        />
    );
}

export function PanelEmptyState({ className, ...props }: HTMLAttributes<HTMLDivElement>) {
    return (
        <div
            className={cn(
                "grid size-full place-content-center justify-items-center gap-2.5 px-5 py-4 text-center text-[12px] leading-relaxed text-muted-foreground",
                className,
            )}
            {...props}
        />
    );
}
