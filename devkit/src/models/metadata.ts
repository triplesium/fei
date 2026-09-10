import { z } from "zod/v4";
import type { ProviderType } from "../settings/providers.js";

const tokens = z.number().int().positive().max(100_000_000).optional();
const modalities = z.array(z.string().min(1).max(64)).max(32).optional();
export const modelMetadataSchema = z.object({
    id: z.string().min(1).max(512),
    name: z.string().min(1).max(512).optional(),
    inputModalities: modalities,
    outputModalities: modalities,
    chat: z.object({
        contextWindow: tokens, maxOutputTokens: tokens,
        reasoning: z.boolean().optional(), tools: z.boolean().optional(),
    }).optional(),
    // These describe the catalog's primary route, not every upstream route.
    topProvider: z.object({ contextWindow: tokens, maxOutputTokens: tokens }).optional(),
    inputSchema: z.unknown().optional(),
    outputSchema: z.unknown().optional(),
    openapi: z.unknown().optional(),
});
export type ModelMetadata = z.infer<typeof modelMetadataSchema>;
export interface MetadataTarget {
    providerId: string;
    type: ProviderType;
    baseUrl?: string;
    apiKey?: string;
}
export type QueryStatus = "found" | "not-found" | "unavailable" | "unsupported";
export interface ResolvedModelMetadata {
    model: ModelMetadata;
    status: QueryStatus;
    stale: boolean;
    sources: Record<string, { source: "override" | "remote" | "builtin"; fetchedAt?: number }>;
}

/** Merge leaves, including explicit false; missing values never erase lower-priority data. */
export function mergeMetadata(id: string, layers: {
    model?: ModelMetadata; source: "override" | "remote" | "builtin"; fetchedAt?: number;
}[]): Pick<ResolvedModelMetadata, "model" | "sources"> {
    const model: ModelMetadata = { id };
    const sources: ResolvedModelMetadata["sources"] = {};
    for (const layer of layers) {
        if (!layer.model || layer.model.id !== id) continue;
        const parsed = modelMetadataSchema.parse(layer.model);
        for (const [key, value] of Object.entries(parsed)) {
            if (key === "id" || value === undefined || value === null) continue;
            if (key === "chat" || key === "topProvider") {
                const fields = Object.fromEntries(Object.entries(value).filter(([, field]) => field !== undefined && field !== null));
                Object.assign(model, { [key]: { ...model[key], ...fields } });
                for (const field of Object.keys(fields)) sources[`${key}.${field}`] = { source: layer.source, fetchedAt: layer.fetchedAt };
            } else {
                Object.assign(model, { [key]: value });
                sources[key] = { source: layer.source, fetchedAt: layer.fetchedAt };
            }
        }
    }
    return { model, sources };
}
