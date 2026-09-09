import { z } from "zod/v4";
import type { EntisiumConfig } from "@entisium/devkit/settings/config";
import type { EditorModelSettings } from "../models/model-settings-store.js";

export const agentSettingsSchema = z.object({
    model: z.object({ provider: z.string().min(1), id: z.string().min(1) }).strict().optional(),
    reasoning: z.enum(["off", "minimal", "low", "medium", "high", "xhigh", "max"]).default("low"),
}).strict();
export type AgentSettings = z.infer<typeof agentSettingsSchema>;
export function parseAgentSettings(value: unknown): AgentSettings {
    const result = agentSettingsSchema.safeParse(value ?? {});
    if (!result.success) throw new Error("Invalid config.yaml agent settings; check model and reasoning.");
    return result.data;
}

const modelSchema = z.object({
    id: z.string().min(1), name: z.string().min(1).optional(), reasoning: z.boolean().default(false),
    contextWindow: z.number().int().min(1024).max(10_000_000),
    maxTokens: z.number().int().min(256),
}).strict().refine((model) => model.maxTokens <= model.contextWindow);

/** Explicit projection: apiKey is never copied into the registry's browser-visible settings. */
export function modelSettingsFromConfig(config: EntisiumConfig): EditorModelSettings {
    const agent = parseAgentSettings(config.agent);
    const providers = Object.entries(config.providers).map(([id, provider]) => {
        const parsed = z.array(modelSchema).safeParse(provider.models);
        if (!parsed.success) throw new Error("Invalid config.yaml provider model catalogue.");
        if (!provider.baseUrl && id !== "openai") throw new Error("Non-OpenAI providers require baseUrl in config.yaml.");
        return {
            id, name: provider.name ?? id, baseUrl: provider.baseUrl ?? "https://api.openai.com/v1", api: provider.api,
            models: parsed.data.map((model) => ({ ...model, name: model.name ?? model.id })),
        };
    });
    if (agent.model && !providers.some((provider) => provider.id === agent.model!.provider && provider.models.some((model) => model.id === agent.model!.id))) {
        throw new Error("config.yaml agent.model must reference a model in providers.<provider>.models.");
    }
    return { version: 2, providers, ...(agent.model ? { active: { providerId: agent.model.provider, modelId: agent.model.id } } : {}) };
}
