import { catalogModelMetadata, fallbackModelMetadata, type ModelMetadataService } from "@entisium/devkit/models/service";
import type { MetadataTarget, ModelMetadata, ResolvedModelMetadata } from "@entisium/devkit/models/metadata";
import type { OpenAICompatibleModelSettings, OpenAICompatibleProviderSettings } from "./model-settings-store.js";

export function metadataTarget(provider: OpenAICompatibleProviderSettings, apiKey?: string): MetadataTarget {
    return { providerId: provider.id, type: provider.type ?? (provider.id === "openai" || provider.id === "openrouter" ? provider.id : "openai-compatible"),
        baseUrl: provider.baseUrl, apiKey };
}

function overrideMetadata(provider: OpenAICompatibleProviderSettings, id: string): ModelMetadata | undefined {
    const override = provider.models.find((model) => model.id === id);
    return override ? { id, name: override.name, chat: { reasoning: override.reasoning, contextWindow: override.contextWindow, maxOutputTokens: override.maxTokens } } : undefined;
}

export function fallbackChatMetadata(provider: OpenAICompatibleProviderSettings, id: string) {
    return fallbackModelMetadata(metadataTarget(provider), id, overrideMetadata(provider, id));
}
export function catalogChatMetadata(provider: OpenAICompatibleProviderSettings, model: ModelMetadata, fetchedAt?: number) {
    return catalogModelMetadata(metadataTarget(provider), model, fetchedAt, overrideMetadata(provider, model.id));
}

export function resolveChatMetadata(service: ModelMetadataService, provider: OpenAICompatibleProviderSettings, id: string, apiKey?: string, signal?: AbortSignal) {
    return service.getModel(metadataTarget(provider, apiKey), id, { override: overrideMetadata(provider, id), signal });
}

/** Operating defaults are applied only at the Agent boundary, never to cached provider facts. */
export function chatRuntimeSettings(metadata: ResolvedModelMetadata): Required<OpenAICompatibleModelSettings> {
    const model = metadata.model;
    const contextWindow = model.chat?.contextWindow ?? 32_768;
    return { id: model.id, name: model.name ?? model.id, reasoning: model.chat?.reasoning ?? false,
        contextWindow, maxTokens: Math.min(model.chat?.maxOutputTokens ?? 4_096, contextWindow) };
}
