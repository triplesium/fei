import { z } from "zod/v4";

export const imageOptionsSchema = z.object({
    resolution: z.enum(["512", "1K", "2K", "4K"]).optional(),
    aspect_ratio: z.enum(["auto", "1:1", "16:9", "9:16", "4:3", "3:4", "3:2", "2:3", "4:5", "5:4", "1:2", "2:1", "1:4", "4:1", "1:8", "8:1", "9:21", "21:9"]).optional(),
    size: z.string().regex(/^(?:512|1K|2K|4K|[1-9][0-9]{0,4}x[1-9][0-9]{0,4})$/).optional()
        .describe("Optional resolution tier or explicit WIDTHxHEIGHT. Prefer resolution and aspect_ratio."),
    quality: z.enum(["auto", "low", "medium", "high", "xhigh", "max"]).optional(),
    background: z.enum(["auto", "transparent", "opaque"]).optional(),
    seed: z.number().int().min(0).max(2147483647).optional(),
}).strict();
export type ImageOptions = z.infer<typeof imageOptionsSchema>;

export const imageGenerationSchema = imageOptionsSchema.extend({
    references: z.array(z.string().regex(/^assets\/(?:[a-zA-Z0-9_-]+\/)*[a-zA-Z0-9_-]+\.png$/)).min(1).max(4).optional()
        .describe("Existing project PNG assets used as visual references, in order."),
    prompt: z.string().trim().min(1).max(32000),
    path: z.string().regex(/^assets\/(?:[a-zA-Z0-9_-]+\/)*[a-zA-Z0-9_-]+\.png$/,
        "Use a PNG path inside assets/ with letters, numbers, underscores or hyphens."),
}).strict();

export type ImageGenerationInput = z.infer<typeof imageGenerationSchema>;
export interface ImageGenerationResult {
    path: string;
    mimeType: "image/png";
    width: number;
    height: number;
    bytes: number;
    model: string;
}
export type ImageGenerationInvoker = (input: ImageGenerationInput, signal?: AbortSignal) => Promise<ImageGenerationResult>;
