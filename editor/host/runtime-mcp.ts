import { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import type { CallToolResult } from "@modelcontextprotocol/sdk/types.js";
import { z } from "zod/v4";
import { NativeRuntime, type InspectionResponse } from "./native-runtime.js";

function textResult(value: unknown): CallToolResult {
    return { content: [{ type: "text", text: JSON.stringify(value) }] };
}

function inspectionResult(response: InspectionResponse): CallToolResult {
    if (!response.ok) return { isError: true, content: [{ type: "text", text: response.error?.message ?? "Inspection failed." }] };
    const result = textResult(response.payload);
    if (response.attachment) result.content.push({ type: "image", mimeType: response.attachment.content_type, data: response.attachment.data });
    return result;
}

export function createRuntimeMcp(runtime: NativeRuntime) {
    const server = new McpServer({ name: "entisium-runtime", version: "0.1.0" }, {
        instructions: "Start a project.yaml with runtime_play; the native game runs hidden and paused between actions. Call play_interfaces, then play_observe and play_step using its action schema. Steps return completed results directly; no status polling is needed. Capture with runtime_capture. Never retry a timed-out action; stop and restart. Read runtime_logs on failure. Stop when finished. No Editor or browser is required.",
    });
    const guarded = (action: () => Promise<CallToolResult>) => action().catch((error: unknown): CallToolResult => ({
        isError: true, content: [{ type: "text", text: error instanceof Error ? error.message : String(error) }],
    }));
    server.registerTool("runtime_play", {
        description: "Start a native project in hidden, supervised playtest mode. Build entisium-runtime-host first. Stop the current session before starting another.",
        inputSchema: { project: z.string().min(1).describe("Absolute path to project.yaml (or relative to MCP working directory).") },
    }, ({ project }) => guarded(async () => textResult(await runtime.start(project))));
    server.registerTool("runtime_stop", { description: "Stop the owned game process and close its controller.", inputSchema: {} }, () => guarded(async () => {
        await runtime.stop(); return textResult(runtime.status());
    }));
    server.registerTool("runtime_status", { description: "Read state and advertised native inspection capabilities.", inputSchema: {}, annotations: { readOnlyHint: true } }, () => textResult(runtime.status()));
    server.registerTool("runtime_logs", { description: "Read the last 64 KiB of game stdout and stderr, retained after stop.", inputSchema: {}, annotations: { readOnlyHint: true } }, () => textResult(runtime.logs()));
    const inspect = (provider: string, payload: unknown) => guarded(async () => inspectionResult(await runtime.inspect(provider, payload)));
    server.registerTool("play_interfaces", { description: "Discover structured game action and observation schemas.", inputSchema: {}, annotations: { readOnlyHint: true } }, () => inspect("play.interfaces", {}));
    server.registerTool("play_observe", {
        description: "Read game state without advancing ticks.", inputSchema: { interface: z.string().min(1) }, annotations: { readOnlyHint: true },
    }, (parameters) => inspect("play.observe", parameters));
    server.registerTool("play_step", {
        description: "Apply one action, advance fixed ticks, clear input and return the completed observation. Omit ticks unless the interface permits overrides.",
        inputSchema: { interface: z.string().min(1), action: z.record(z.string(), z.unknown()), ticks: z.number().int().positive().optional() },
    }, (parameters) => inspect("play.step", parameters));
    server.registerTool("runtime_capture", { description: "Capture the hidden game's current viewport as PNG without advancing simulation.", inputSchema: {}, annotations: { readOnlyHint: true } }, () => inspect("play.capture", {}));
    server.registerTool("play_segment", {
        description: "Run isolated Luau returning function(ctx), called each fixed tick with ctx.tick and ctx.observation. Return {action={...}} or {stop='reason'}. Bounded to 600 ticks. Returns final result directly without polling.",
        inputSchema: { interface: z.string().min(1), source: z.string().min(1).max(65536), max_ticks: z.number().int().min(1).max(600) },
    }, (parameters) => inspect("play.segment", parameters));
    server.registerTool("runtime_inspect", {
        description: "Invoke an advertised inspection (including ECS and checkpoints). Read runtime_status for provider IDs and request schemas.",
        inputSchema: { provider: z.string().min(1), payload: z.record(z.string(), z.unknown()) },
    }, ({ provider, payload }) => inspect(provider, payload));
    return server;
}
