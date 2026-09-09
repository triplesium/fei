import { z } from "zod/v4";

const id = z.string().min(1);
export const runtimeToolDefinitions = [
    { name: "runtime_play", description: "Start the saved project in a hidden native window, paused between actions. No browser is needed. Stop the current session before starting another.", schema: z.object({ project: id.optional() }).strict() },
    { name: "runtime_stop", description: "Stop the owned native game process.", schema: z.object({}).strict() },
    { name: "runtime_status", description: "Read native session state and inspection capabilities.", schema: z.object({}).strict() },
    { name: "runtime_logs", description: "Read retained native game stdout and stderr.", schema: z.object({}).strict() },
    { name: "play_interfaces", description: "Discover native game action and observation schemas.", schema: z.object({}).strict() },
    { name: "play_observe", description: "Read native game state without advancing ticks.", schema: z.object({ interface: id }).strict() },
    { name: "play_step", description: "Apply an action and advance fixed ticks. Returns the completed observation; never poll or retry a timed-out action. Cancellation stops the native runtime.", schema: z.object({ interface: id, action: z.record(z.string(), z.unknown()), ticks: z.number().int().positive().optional() }).strict() },
    { name: "play_segment", description: "Run isolated Luau returning function(ctx), once per tick, returning {action={...}} or {stop='reason'}. Returns the final observation. Cancellation stops the native runtime.", schema: z.object({ interface: id, source: id.max(65536), max_ticks: z.number().int().min(1).max(600) }).strict() },
    { name: "runtime_capture", description: "Capture the native viewport as PNG without advancing simulation.", schema: z.object({}).strict() },
    { name: "runtime_inspect", description: "Invoke an inspection advertised by native runtime_status, including ECS and checkpoints.", schema: z.object({ provider: id, payload: z.record(z.string(), z.unknown()) }).strict() },
] as const;

export interface RuntimeToolResult {
    value: unknown;
    image?: { mimeType: string; data: string };
}
export type RuntimeToolInvoker = (name: string, parameters: unknown, signal?: AbortSignal) => Promise<RuntimeToolResult>;
