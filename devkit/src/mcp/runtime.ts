import { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import type { NativeRuntime } from "../runtime/native-runtime.js";
import { RuntimeSession } from "../runtime/session.js";
import { runtimeToolDefinitions } from "../contracts/runtime-tools.js";

export function createRuntimeMcp(runtime: NativeRuntime) {
    const session = new RuntimeSession(runtime);
    const server = new McpServer({ name: "entisium-runtime", version: "0.1.0" }, {
        instructions: "Start a project.yaml with runtime_play. The native game runs hidden and paused between actions. Discover play_interfaces, then observe and step. Results are final; never retry timed-out actions. Stop when finished. No Editor or browser is required.",
    });
    for (const definition of runtimeToolDefinitions) {
        server.registerTool(definition.name, {
            description: definition.description,
            inputSchema: definition.schema,
            annotations: {
                readOnlyHint: ["runtime_status", "runtime_logs", "play_interfaces", "play_observe", "runtime_capture"].includes(definition.name),
            },
        }, async (parameters: Record<string, unknown>, extra: { signal: AbortSignal }) => {
            try {
                const result = await session.invoke(definition.name, parameters, extra.signal);
                return { content: [
                    { type: "text" as const, text: JSON.stringify(result.value ?? null) },
                    ...(result.image ? [{ type: "image" as const, ...result.image }] : []),
                ] };
            } catch (error) {
                return { isError: true, content: [{ type: "text" as const, text: error instanceof Error ? error.message : String(error) }] };
            }
        });
    }
    return server;
}
