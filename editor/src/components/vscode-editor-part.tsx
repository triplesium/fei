import { useLayoutEffect, useRef } from "react";
import { mountVscodeEditorHost } from "../services/vscode-editor-host";

export function VscodeEditorPart() {
    const containerRef = useRef<HTMLDivElement>(null);

    useLayoutEffect(() => {
        if (!containerRef.current) return;
        mountVscodeEditorHost(containerRef.current);
    }, []);

    return <div ref={containerRef} className="min-h-0 flex-1" />;
}
