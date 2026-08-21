import type { PropsWithChildren, ReactNode } from "react";

interface PanelHeaderProps {
    title: string;
    detail?: ReactNode;
    actions?: ReactNode;
}

export function PanelHeader({ title, detail, actions }: PanelHeaderProps) {
    return (
        <header className="panel-header">
            <div className="panel-title">
                <span>{title}</span>
                {detail && <span className="panel-detail">{detail}</span>}
            </div>
            {actions && <div className="panel-actions">{actions}</div>}
        </header>
    );
}

export function ToolPanel({ children, className = "" }: PropsWithChildren<{ className?: string }>) {
    return <section className={`tool-panel ${className}`}>{children}</section>;
}
