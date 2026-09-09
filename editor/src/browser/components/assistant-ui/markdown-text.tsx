import "@assistant-ui/react-markdown/styles/dot.css";

import {
    MarkdownTextPrimitive,
    unstable_memoizeMarkdownComponents as memoizeMarkdownComponents,
    useIsMarkdownCodeBlock,
    type CodeHeaderProps,
} from "@assistant-ui/react-markdown";
import { Check, Copy } from "lucide-react";
import { useState } from "react";
import remarkGfm from "remark-gfm";
import { cn } from "@/lib/utils";
import { TooltipIconButton } from "./tooltip-icon-button";

function CodeHeader({ language, code }: CodeHeaderProps) {
    const [copied, setCopied] = useState(false);
    const copy = async () => {
        if (!navigator.clipboard) return;
        await navigator.clipboard.writeText(code);
        setCopied(true);
        window.setTimeout(() => setCopied(false), 2_000);
    };

    return (
        <div className="flex items-center justify-between rounded-t-xl border border-b-0 border-border/50 bg-muted/50 px-3.5 py-1.5 text-xs text-muted-foreground">
            <span className="font-mono lowercase">{language || "code"}</span>
            <TooltipIconButton
                tooltip={copied ? "Copied" : "Copy code"}
                className="size-6"
                type="button"
                onClick={() => void copy()}
            >
                {copied ? <Check className="size-3.5" /> : <Copy className="size-3.5" />}
            </TooltipIconButton>
        </div>
    );
}

const components = memoizeMarkdownComponents({
    h1: ({ className, ...props }) => (
        <h1 className={cn("mt-4 mb-1.5 text-lg font-semibold first:mt-0", className)} {...props} />
    ),
    h2: ({ className, ...props }) => (
        <h2 className={cn("mt-4 mb-1.5 text-base font-semibold first:mt-0", className)} {...props} />
    ),
    h3: ({ className, ...props }) => (
        <h3 className={cn("mt-3 mb-1 text-sm font-semibold first:mt-0", className)} {...props} />
    ),
    p: ({ className, ...props }) => (
        <p className={cn("my-2 leading-[22px] first:mt-0 last:mb-0", className)} {...props} />
    ),
    ul: ({ className, ...props }) => (
        <ul className={cn("my-2 ms-5 list-disc [&>li]:mt-0.5", className)} {...props} />
    ),
    ol: ({ className, ...props }) => (
        <ol className={cn("my-2 ms-5 list-decimal [&>li]:mt-0.5", className)} {...props} />
    ),
    blockquote: ({ className, ...props }) => (
        <blockquote className={cn("border-s-2 border-border ps-4 italic text-muted-foreground", className)} {...props} />
    ),
    a: ({ className, ...props }) => (
        <a className={cn("font-medium text-primary underline underline-offset-4", className)} target="_blank" rel="noreferrer" {...props} />
    ),
    hr: ({ className, ...props }) => <hr className={cn("my-3 border-border", className)} {...props} />,
    table: ({ className, ...props }) => (
        <div className="my-3 overflow-x-auto rounded-lg border border-border">
            <table className={cn("w-full border-collapse text-left", className)} {...props} />
        </div>
    ),
    th: ({ className, ...props }) => (
        <th className={cn("border-b border-r border-border bg-muted/50 px-3 py-2 font-medium last:border-r-0", className)} {...props} />
    ),
    td: ({ className, ...props }) => (
        <td className={cn("border-b border-r border-border px-3 py-2 last:border-r-0", className)} {...props} />
    ),
    tr: ({ className, ...props }) => (
        <tr className={cn("last:[&>td]:border-b-0", className)} {...props} />
    ),
    pre: ({ className, ...props }) => (
        <pre className={cn("overflow-x-auto rounded-t-none rounded-b-xl border border-t-0 border-border/50 bg-muted/30 p-3.5 font-mono text-[13px] leading-relaxed", className)} {...props} />
    ),
    code: function Code({ className, ...props }) {
        const isBlock = useIsMarkdownCodeBlock();
        return (
            <code
                className={cn(
                    !isBlock && "rounded-md bg-muted px-1 py-0.5 font-mono text-[0.9em]",
                    className,
                )}
                {...props}
            />
        );
    },
    CodeHeader,
});

export function MarkdownText() {
    return (
        <MarkdownTextPrimitive
            remarkPlugins={[remarkGfm]}
            components={components}
            className="aui-md text-sm leading-[22px] text-foreground [overflow-wrap:anywhere]"
        />
    );
}
