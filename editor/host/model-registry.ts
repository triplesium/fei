import {
    createModels,
    createProvider,
    type Api,
    type Context,
    type CredentialStore,
    type Model,
    type SimpleStreamOptions,
} from "@earendil-works/pi-ai";
import { openAICompletionsApi } from "@earendil-works/pi-ai/api/openai-completions.lazy";
import { openAIResponsesApi } from "@earendil-works/pi-ai/api/openai-responses.lazy";
import {
    type EditorModelSettings,
    type EditorModelSettingsStore,
    type OpenAICompatibleModelSettings,
    type OpenAICompatibleProviderSettings,
} from "./model-settings-store.js";

export interface ModelProviderSummary {
    id: string;
    name: string;
    baseUrl: string;
    api: OpenAICompatibleProviderSettings["api"];
    configured: boolean;
    models: Model<Api>[];
}

export interface ModelRegistrySnapshot {
    active: { providerId: string; modelId: string };
    providers: ModelProviderSummary[];
}

export interface ConfigureModelInput {
    providerId: string;
    modelId: string;
    apiKey?: string;
    provider?: OpenAICompatibleProviderSettings;
}

export interface ConfigureProviderInput {
    provider: Omit<OpenAICompatibleProviderSettings, "models">;
    apiKey?: string;
}

export interface ConfigureRegistryModelInput {
    providerId: string;
    previousModelId?: string;
    model: OpenAICompatibleModelSettings;
}

const zeroCost = { input: 0, output: 0, cacheRead: 0, cacheWrite: 0 };

function normalizedUrl(value: string): string {
    const source = value.trim().replace(/\/+$/, "");
    let url: URL;
    try {
        url = new URL(source);
    } catch {
        throw new Error("API Base URL must be a valid URL.");
    }
    if (url.protocol !== "https:" && url.protocol !== "http:") {
        throw new Error("API Base URL must use HTTP or HTTPS.");
    }
    if (url.username || url.password || url.search || url.hash) {
        throw new Error("API Base URL cannot contain credentials, a query, or a fragment.");
    }
    return source;
}

function nonEmpty(value: string, label: string, maximum = 160): string {
    const next = value.trim();
    if (!next) throw new Error(`${label} is required.`);
    if (next.length > maximum) throw new Error(`${label} is too long.`);
    return next;
}

function identifier(value: string, label: string): string {
    const next = nonEmpty(value, label, 120);
    if (!/^[A-Za-z0-9][A-Za-z0-9._:/-]*$/.test(next)) {
        throw new Error(`${label} may only contain letters, numbers, dots, dashes, underscores, slashes, and colons.`);
    }
    return next;
}

function validApiKey(value: string): boolean {
    return value.length >= 8 && value.length <= 4_096 && /^[\x21-\x7e]+$/.test(value);
}

function integer(value: number, label: string, minimum: number, maximum: number): number {
    if (!Number.isInteger(value) || value < minimum || value > maximum) {
        throw new Error(`${label} must be between ${minimum} and ${maximum}.`);
    }
    return value;
}

function normalizeModel(input: OpenAICompatibleModelSettings): OpenAICompatibleModelSettings {
    const contextWindow = integer(input.contextWindow, "Context window", 1_024, 10_000_000);
    return {
        id: identifier(input.id, "Model ID"),
        name: nonEmpty(input.name, "Model name"),
        reasoning: Boolean(input.reasoning),
        contextWindow,
        maxTokens: integer(input.maxTokens, "Maximum output tokens", 256, contextWindow),
    };
}

function normalizeProvider(
    input: OpenAICompatibleProviderSettings,
): OpenAICompatibleProviderSettings {
    if (!Array.isArray(input.models)) throw new Error("Provider models must be a list.");
    const models = input.models.map(normalizeModel);
    if (new Set(models.map((model) => model.id)).size !== models.length) {
        throw new Error("Model IDs must be unique within a provider.");
    }
    return {
        id: identifier(input.id, "Provider ID"),
        name: nonEmpty(input.name, "Provider name"),
        baseUrl: normalizedUrl(input.baseUrl),
        api: input.api === "chat-completions" ? "chat-completions" : "responses",
        models,
    };
}

function runtimeProvider(settings: OpenAICompatibleProviderSettings) {
    const api = settings.api === "responses" ? "openai-responses" : "openai-completions";
    const models: Model<Api>[] = settings.models.map((model) => ({
        id: model.id,
        name: model.name,
        api,
        provider: settings.id,
        baseUrl: settings.baseUrl,
        reasoning: model.reasoning,
        input: ["text"],
        cost: zeroCost,
        contextWindow: model.contextWindow,
        maxTokens: model.maxTokens,
        ...(settings.api === "responses"
            ? {
                  thinkingLevelMap: {
                      off: "none",
                      minimal: "minimal",
                      low: "low",
                      medium: "medium",
                      high: "high",
                      xhigh: "xhigh",
                      max: "max",
                  },
                  compat: {
                      supportsDeveloperRole: false,
                      supportsLongCacheRetention: false,
                  },
              }
            : {}),
    }));
    return createProvider({
        id: settings.id,
        name: settings.name,
        baseUrl: settings.baseUrl,
        auth: {
            apiKey: {
                name: `${settings.name} API key`,
                resolve: async ({ credential, signal }) => {
                    signal.throwIfAborted();
                    return credential?.key
                        ? { auth: { apiKey: credential.key }, source: "stored credential" }
                        : undefined;
                },
            },
        },
        models,
        api: settings.api === "responses" ? openAIResponsesApi() : openAICompletionsApi(),
    });
}

export class HostModelRegistry {
    private readonly models;
    private readonly readyPromise: Promise<void>;
    private readonly warnedCredentialProviders = new Set<string>();
    private settings!: EditorModelSettings;

    constructor(
        private readonly credentials: CredentialStore,
        private readonly settingsStore: EditorModelSettingsStore,
    ) {
        this.models = createModels({ credentials });
        this.readyPromise = this.initialize();
    }

    async snapshot(): Promise<ModelRegistrySnapshot> {
        await this.readyPromise;
        const active = this.resolveActive();
        const providers = await Promise.all(
            this.settings.providers.map(async (settings): Promise<ModelProviderSummary> => {
                const provider = this.models.getProvider(settings.id);
                if (!provider) throw new Error(`Provider ${settings.id} is unavailable.`);
                return {
                    id: settings.id,
                    name: settings.name,
                    baseUrl: settings.baseUrl,
                    api: settings.api,
                    configured: await this.isConfigured(settings.id),
                    models: provider.getModels().map((model) => structuredClone(model)),
                };
            }),
        );
        return { active, providers };
    }

    async configure(input: ConfigureModelInput): Promise<ModelRegistrySnapshot> {
        await this.readyPromise;
        const providerId = identifier(input.providerId, "Provider ID");
        const modelId = identifier(input.modelId, "Model ID");
        const nextSettings = structuredClone(this.settings);

        if (input.provider) {
            const provider = normalizeProvider(input.provider);
            if (provider.id !== providerId) {
                throw new Error("The configured provider must match the selected Provider ID.");
            }
            const existingIndex = nextSettings.providers.findIndex((candidate) => candidate.id === provider.id);
            if (existingIndex >= 0) nextSettings.providers[existingIndex] = provider;
            else nextSettings.providers.push(provider);
            this.models.setProvider(runtimeProvider(provider));
        }

        if (!this.models.getModel(providerId, modelId)) {
            throw new Error("The selected model is unavailable.");
        }
        if (input.apiKey !== undefined) {
            const apiKey = input.apiKey.trim();
            if (!validApiKey(apiKey)) {
                throw new Error(
                    "API key must contain between 8 and 4096 printable ASCII characters without spaces.",
                );
            }
            await this.credentials.modify(providerId, async () => ({ type: "api_key", key: apiKey }));
        }
        nextSettings.active = { providerId, modelId };
        await this.settingsStore.write(nextSettings);
        this.settings = nextSettings;
        return this.snapshot();
    }

    async configureProvider(input: ConfigureProviderInput): Promise<ModelRegistrySnapshot> {
        await this.readyPromise;
        const providerInput = input.provider;
        const existing = this.settings.providers.find((provider) => provider.id === providerInput.id);
        const provider = normalizeProvider({
            ...providerInput,
            models: existing?.models ?? [],
        });
        const nextSettings = structuredClone(this.settings);
        const existingIndex = nextSettings.providers.findIndex((candidate) => candidate.id === provider.id);
        if (existingIndex >= 0) nextSettings.providers[existingIndex] = provider;
        else nextSettings.providers.push(provider);

        if (input.apiKey !== undefined) {
            const apiKey = input.apiKey.trim();
            if (!validApiKey(apiKey)) {
                throw new Error(
                    "API key must contain between 8 and 4096 printable ASCII characters without spaces.",
                );
            }
            await this.credentials.modify(provider.id, async () => ({ type: "api_key", key: apiKey }));
        }
        await this.settingsStore.write(nextSettings);
        this.settings = nextSettings;
        this.models.setProvider(runtimeProvider(provider));
        return this.snapshot();
    }

    async configureRegistryModel(input: ConfigureRegistryModelInput): Promise<ModelRegistrySnapshot> {
        await this.readyPromise;
        const providerId = identifier(input.providerId, "Provider ID");
        const model = normalizeModel(input.model);
        const previousModelId = input.previousModelId
            ? identifier(input.previousModelId, "Previous Model ID")
            : undefined;
        const nextSettings = structuredClone(this.settings);
        const provider = nextSettings.providers.find((candidate) => candidate.id === providerId);
        if (!provider) throw new Error("Unknown model provider.");

        const existingIndex = previousModelId
            ? provider.models.findIndex((candidate) => candidate.id === previousModelId)
            : -1;
        if (previousModelId && existingIndex < 0) throw new Error("The model to edit is unavailable.");
        if (
            provider.models.some(
                (candidate, index) => candidate.id === model.id && index !== existingIndex,
            )
        ) {
            throw new Error("Model IDs must be unique within a provider.");
        }
        if (existingIndex >= 0) provider.models[existingIndex] = model;
        else provider.models.push(model);

        if (
            previousModelId &&
            nextSettings.active?.providerId === providerId &&
            nextSettings.active.modelId === previousModelId
        ) {
            nextSettings.active.modelId = model.id;
        }
        nextSettings.active ??= { providerId, modelId: model.id };
        await this.settingsStore.write(nextSettings);
        this.settings = nextSettings;
        this.models.setProvider(runtimeProvider(provider));
        return this.snapshot();
    }

    async deleteModel(providerIdValue: string, modelIdValue: string): Promise<ModelRegistrySnapshot> {
        await this.readyPromise;
        const providerId = identifier(providerIdValue, "Provider ID");
        const modelId = identifier(modelIdValue, "Model ID");
        const nextSettings = structuredClone(this.settings);
        const providerIndex = nextSettings.providers.findIndex((provider) => provider.id === providerId);
        const provider = nextSettings.providers[providerIndex];
        if (!provider || !provider.models.some((model) => model.id === modelId)) {
            throw new Error("The model to delete is unavailable.");
        }

        const modelCount = nextSettings.providers.reduce(
            (total, candidate) => total + candidate.models.length,
            0,
        );
        if (modelCount === 1) {
            throw new Error("At least one model must remain configured.");
        }
        provider.models = provider.models.filter((model) => model.id !== modelId);
        this.models.setProvider(runtimeProvider(provider));

        if (nextSettings.active?.providerId === providerId && nextSettings.active.modelId === modelId) {
            const fallbackProvider = nextSettings.providers[0];
            nextSettings.active = {
                providerId: fallbackProvider.id,
                modelId: fallbackProvider.models[0].id,
            };
        }
        await this.settingsStore.write(nextSettings);
        this.settings = nextSettings;
        return this.snapshot();
    }

    async deleteProvider(providerIdValue: string): Promise<ModelRegistrySnapshot> {
        await this.readyPromise;
        const providerId = identifier(providerIdValue, "Provider ID");
        const nextSettings = structuredClone(this.settings);
        const provider = nextSettings.providers.find((candidate) => candidate.id === providerId);
        if (!provider) throw new Error("Unknown model provider.");
        const remainingModelCount = nextSettings.providers.reduce(
            (total, candidate) => total + (candidate.id === providerId ? 0 : candidate.models.length),
            0,
        );
        if (remainingModelCount === 0) throw new Error("At least one model must remain configured.");

        nextSettings.providers = nextSettings.providers.filter((candidate) => candidate.id !== providerId);
        if (nextSettings.active?.providerId === providerId) {
            const fallbackProvider = nextSettings.providers.find((candidate) => candidate.models.length > 0)!;
            nextSettings.active = {
                providerId: fallbackProvider.id,
                modelId: fallbackProvider.models[0].id,
            };
        }
        await this.settingsStore.write(nextSettings);
        this.settings = nextSettings;
        this.models.deleteProvider(providerId);
        await this.credentials.delete(providerId);
        return this.snapshot();
    }

    async deleteCredential(providerId: string): Promise<ModelRegistrySnapshot> {
        await this.readyPromise;
        if (!this.models.getProvider(providerId)) throw new Error("Unknown model provider.");
        await this.credentials.delete(providerId);
        return this.snapshot();
    }

    async activeModel(): Promise<{ provider: ModelProviderSummary; model: Model<Api> }> {
        const snapshot = await this.snapshot();
        const provider = snapshot.providers.find((candidate) => candidate.id === snapshot.active.providerId);
        const model = provider?.models.find((candidate) => candidate.id === snapshot.active.modelId);
        if (!provider || !model) throw new Error("The active Editor model is unavailable.");
        return { provider, model };
    }

    async getModel(providerId: string, modelId: string): Promise<Model<Api> | undefined> {
        await this.readyPromise;
        return this.models.getModel(providerId, modelId);
    }

    streamSimple(model: Model<Api>, context: Context, options?: SimpleStreamOptions) {
        return this.models.streamSimple(model, context, options);
    }

    private async initialize(): Promise<void> {
        const settings = await this.settingsStore.read();
        settings.providers = settings.providers.map(normalizeProvider);
        if (new Set(settings.providers.map((provider) => provider.id)).size !== settings.providers.length) {
            throw new Error("Provider IDs must be unique.");
        }
        this.settings = settings;
        for (const provider of settings.providers) this.models.setProvider(runtimeProvider(provider));
    }

    private async isConfigured(providerId: string): Promise<boolean> {
        try {
            const credential = await this.credentials.read(providerId);
            if (
                credential?.type === "api_key" &&
                (typeof credential.key !== "string" || !validApiKey(credential.key))
            ) {
                return false;
            }
            return Boolean(await this.models.checkAuth(providerId));
        } catch {
            if (!this.warnedCredentialProviders.has(providerId)) {
                this.warnedCredentialProviders.add(providerId);
                console.warn(
                    `[fei editor] ${providerId} credentials are unavailable; treating the provider as unconfigured.`,
                );
            }
            return false;
        }
    }

    private resolveActive(): { providerId: string; modelId: string } {
        const configuredModel = process.env.FEI_EDITOR_MODEL?.trim();
        if (configuredModel) {
            for (const provider of this.settings.providers) {
                if (this.models.getModel(provider.id, configuredModel)) {
                    return { providerId: provider.id, modelId: configuredModel };
                }
            }
        }
        const active = this.settings.active;
        if (active && this.models.getModel(active.providerId, active.modelId)) return { ...active };
        const provider = this.settings.providers.find((candidate) => candidate.models.length > 0);
        const model = provider?.models[0];
        if (!provider || !model) throw new Error("No OpenAI-compatible models are registered.");
        return { providerId: provider.id, modelId: model.id };
    }
}
