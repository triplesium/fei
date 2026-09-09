import type { AgentTool } from "@earendil-works/pi-agent-core";
import type { TSchema } from "typebox";
import { z } from "zod/v4";
import { imageGenerationSchema, type ImageGenerationInvoker } from "@entisium/devkit/contracts/image-generation";

export function createImageGenerationTools(invoke: ImageGenerationInvoker): AgentTool<any>[] {
    return [{
        name: "generate_image", label: "Generate image",
        description: "Generate a new image from a detailed text prompt and save it as a PNG project asset. Use resolution and aspect_ratio for dimensions, or size for explicit pixels. Omitted options use configured defaults. seed requires OpenRouter. Choose a new path inside assets/; existing files are never overwritten. This calls a paid image API and may take several minutes. Returns the saved path and dimensions. Do not automatically retry a failed or cancelled request; it may already have incurred a charge.",
        parameters: z.toJSONSchema(imageGenerationSchema) as TSchema,
        execute: async (_id, parameters, signal) => {
            signal?.throwIfAborted();
            const result = await invoke(imageGenerationSchema.parse(parameters), signal);
            return { content: [{ type: "text" as const, text: JSON.stringify(result) }], details: result };
        },
    }];
}
