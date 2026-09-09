import { imageOptionsSchema } from "../contracts/image-generation.js";
import { z } from "zod/v4";

const text = z.string().trim().min(1);
const providerSchema = z.object({
    name: text.optional(),
    baseUrl: z.string().url().refine((value) => {
        const url = new URL(value);
        return ["http:", "https:"].includes(url.protocol) && !url.username && !url.password && !url.search && !url.hash;
    }).optional(),
    apiKey: z.string().trim().min(8).max(4096).regex(/^[\x21-\x7e]+$/).optional(),
    api: z.enum(["responses", "chat-completions"]).default("responses"),
    // Model catalogue details belong to the consuming Agent package.
    models: z.array(z.unknown()).default([]),
}).strict();

export const configSchema = z.object({
    version: z.literal(1),
    providers: z.record(z.string().regex(/^[A-Za-z0-9][A-Za-z0-9._-]*$/), providerSchema).default({}),
    imageGeneration: z.object({
        api: z.enum(["openai-images", "openrouter-images"]).default("openai-images"),
        provider: text.default("openai"),
        model: text.default("gpt-image-2"),
        timeoutMs: z.number().int().min(1000).max(30 * 60_000).default(10 * 60_000),
        defaults: imageOptionsSchema.prefault({}),
    }).prefault({}),
    runtime: z.object({ executable: text.default("auto") }).prefault({}),
    agent: z.unknown().optional(),
    editor: z.unknown().optional(),
}).strict();

export type EntisiumConfig = z.infer<typeof configSchema>;

export function parseConfig(value: unknown): EntisiumConfig {
    const parsed = configSchema.safeParse(value);
    if (!parsed.success) {
        // Do not include Zod's received values, source snippets or arbitrary property names.
        const fields = parsed.error.issues.map((issue) => issue.path
            .map((part, index) => index > 0 && issue.path[0] === "providers" && index === 1 ? "<provider>" : String(part)).join("."));
        throw new Error(`Invalid config.yaml fields: ${[...new Set(fields)].join(", ") || "root"}.`);
    }
    return parsed.data;
}
