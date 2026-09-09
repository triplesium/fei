import type { AgentTool } from "@earendil-works/pi-agent-core";
import type { TSchema } from "typebox";
import { z } from "zod/v4";

import { runtimeToolDefinitions, type RuntimeToolInvoker } from "@entisium/devkit/contracts/runtime-tools";

/** Browser-safe tool contracts shared by the editor, CLI and MCP. */
export function createNativeAgentTools(invoke: RuntimeToolInvoker): AgentTool<any>[] {
    return runtimeToolDefinitions.map((definition) => ({
        name: `native_${definition.name}`,
        label: `Native: ${definition.name}`,
        description: definition.description,
        parameters: z.toJSONSchema(definition.schema) as TSchema,
        execute: async (_id, parameters, signal) => {
            signal?.throwIfAborted();
            const result = await invoke(definition.name, definition.schema.parse(parameters), signal);
            signal?.throwIfAborted();
            return {
                content: [
                    { type: "text" as const, text: JSON.stringify(result.value ?? null) },
                    ...(result.image ? [{ type: "image" as const, ...result.image }] : []),
                ],
                details: { command: `native_${definition.name}`, value: result.image ?? result.value },
            };
        },
    }));
}
