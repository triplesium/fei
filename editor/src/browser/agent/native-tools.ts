import { createNativeAgentTools } from "@entisium/agent/tools/runtime";
import type { RuntimeToolResult } from "@entisium/devkit/contracts/runtime-tools";
import { editorHost } from "../services/editor-host-client";

export function createEditorNativeTools() {
    return createNativeAgentTools(async (name, parameters, signal) => {
        signal?.throwIfAborted();
        // Wait for the host to acknowledge cleanup, rather than abandoning an action fetch.
        let cleanup: Promise<unknown> | undefined;
        const invoke = (tool: string, input: unknown, requestSignal?: AbortSignal) => editorHost.json<RuntimeToolResult>("/api/v1/native-runtime", {
            signal: requestSignal, method: "POST", headers: { "Content-Type": "application/json" },
            body: JSON.stringify({ name: tool, parameters: input }),
        });
        const cancel = () => { cleanup ??= invoke("runtime_stop", {}); void cleanup.catch(() => {}); };
        signal?.addEventListener("abort", cancel, { once: true });
        try { return await invoke(name, parameters, signal); }
        finally {
            signal?.removeEventListener("abort", cancel);
            if (cleanup) await cleanup;
        }
    });
}
