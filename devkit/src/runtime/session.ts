import { resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { NativeRuntime } from "./native-runtime.js";
import { runtimeToolDefinitions, type RuntimeToolResult } from "../contracts/runtime-tools.js";

export function defaultRuntimeExecutable(): string {
    return resolve(process.env.ETS_RUNTIME_HOST_PATH ?? resolve(
        fileURLToPath(new URL("../../../", import.meta.url)), "build",
        process.platform === "win32" ? "windows" : process.platform,
        process.arch, "debug", `entisium-runtime-host${process.platform === "win32" ? ".exe" : ""}`,
    ));
}

/** Owns one native runtime; independent of MCP, the editor relay and the agent. */
export class RuntimeSession {
    constructor(
        readonly runtime = new NativeRuntime(defaultRuntimeExecutable()),
        private readonly projectFile?: () => Promise<string>,
    ) {}

    async invoke(name: string, parameters: unknown, signal?: AbortSignal): Promise<RuntimeToolResult> {
        signal?.throwIfAborted();
        const definition = runtimeToolDefinitions.find((tool) => tool.name === name);
        if (!definition) throw new Error(`Unknown native runtime tool: ${name}`);
        const input = definition.schema.parse(parameters) as Record<string, any>;
        let cleanup: Promise<void> | undefined;
        const cancel = () => { cleanup ??= this.runtime.stop(); void cleanup.catch(() => {}); };
        signal?.addEventListener("abort", cancel, { once: true });
        try {
            switch (name) {
                case "runtime_play": {
                    const boundProject = await this.projectFile?.();
                    if (boundProject && input.project && resolve(input.project) !== resolve(boundProject)) {
                        throw new Error("This session is bound to its current project.");
                    }
                    const project = boundProject ?? input.project;
                    if (!project) throw new Error("A project.yaml path is required.");
                    signal?.throwIfAborted();
                    return { value: await this.runtime.start(project) };
                }
                case "runtime_stop": await this.runtime.stop(); return { value: this.runtime.status() };
                case "runtime_status": return { value: this.runtime.status() };
                case "runtime_logs": return { value: this.runtime.logs() };
            }
            const providers: Record<string, string> = {
                play_interfaces: "play.interfaces", play_observe: "play.observe", play_step: "play.step",
                play_segment: "play.segment", runtime_capture: "play.capture",
            };
            const response = await this.runtime.inspect(
                name === "runtime_inspect" ? input.provider : providers[name],
                name === "runtime_inspect" ? input.payload : input,
            );
            if (!response.ok) throw new Error(response.error?.message ?? "Runtime inspection failed.");
            return {
                value: response.payload,
                ...(response.attachment ? { image: { mimeType: response.attachment.content_type, data: response.attachment.data } } : {}),
            };
        } finally {
            signal?.removeEventListener("abort", cancel);
            if (cleanup) await cleanup;
        }
    }
    stop(): Promise<void> { return this.runtime.stop(); }
}
