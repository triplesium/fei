import type { AuthOperationOptions, Credential, CredentialStore } from "@earendil-works/pi-ai";
import { YamlConfigStore, loadConfig } from "@entisium/devkit/settings/yaml-store";
import { EncryptedCredentialStore } from "../models/credential-store.js";
import { FileEditorModelSettingsStore, type EditorModelSettings, type EditorModelSettingsStore } from "../models/model-settings-store.js";
import { modelSettingsFromConfig, parseAgentSettings } from "../settings/config.js";

export class YamlCredentialStore implements CredentialStore {
    constructor(private readonly config: YamlConfigStore) {}
    async read(providerId: string, options?: AuthOperationOptions): Promise<Credential | undefined> {
        options?.signal?.throwIfAborted();
        const config = await this.config.read();
        options?.signal?.throwIfAborted();
        const key = Object.hasOwn(config.providers, providerId) ? config.providers[providerId].apiKey : undefined;
        return key ? { type: "api_key", key } : undefined;
    }
    async list(options?: AuthOperationOptions) {
        options?.signal?.throwIfAborted();
        return Object.entries((await this.config.read()).providers)
            .filter(([, provider]) => Boolean(provider.apiKey)).map(([providerId]) => ({ providerId, type: "api_key" as const }));
    }
    async modify(providerId: string, edit: (current: Credential | undefined) => Promise<Credential | undefined>, options?: AuthOperationOptions): Promise<Credential | undefined> {
        let result: Credential | undefined;
        await this.config.update(async (config) => {
            options?.signal?.throwIfAborted();
            const provider = Object.hasOwn(config.providers, providerId) ? config.providers[providerId] : { api: "responses" as const, models: [] };

            result = await edit(provider.apiKey ? { type: "api_key", key: provider.apiKey } : undefined);
            options?.signal?.throwIfAborted();
            if (result !== undefined) {
                if (result.type !== "api_key") throw new Error("config.yaml supports API key credentials only.");
                provider.apiKey = result.key;
                Object.defineProperty(config.providers, providerId, { value: provider, enumerable: true, writable: true, configurable: true });
            }
        });
        return result;
    }
    async delete(providerId: string, options?: AuthOperationOptions): Promise<void> {
        await this.config.update((config) => {
            options?.signal?.throwIfAborted();
            if (Object.hasOwn(config.providers, providerId)) delete config.providers[providerId].apiKey;
        });
    }
}

export class YamlModelSettingsStore implements EditorModelSettingsStore {
    constructor(private readonly config: YamlConfigStore) {}
    async read(): Promise<EditorModelSettings> { return modelSettingsFromConfig(await this.config.read()); }
    async write(settings: EditorModelSettings): Promise<void> {
        await this.config.update((config) => {
            const providers = Object.fromEntries(settings.providers.map(({ id, ...provider }) => [id, {
                ...provider,
                ...(Object.hasOwn(config.providers, id) && config.providers[id].apiKey ? { apiKey: config.providers[id].apiKey } : {}),
            }]));
            config.providers = providers;
            config.agent = { ...parseAgentSettings(config.agent),
                model: settings.active ? { provider: settings.active.providerId, id: settings.active.modelId } : undefined };
            modelSettingsFromConfig(config);
        });
    }
}

export async function loadHostConfiguration(path?: string, projectDirectory?: string) {
    const { store, config } = await loadConfig(path, projectDirectory);
    if (!config) {
        return { credentials: new EncryptedCredentialStore() as CredentialStore,
            modelSettingsStore: new FileEditorModelSettingsStore() as EditorModelSettingsStore,
            config: undefined, store, agent: parseAgentSettings(undefined) };
    }
    modelSettingsFromConfig(config);
    return { credentials: new YamlCredentialStore(store) as CredentialStore,
        modelSettingsStore: new YamlModelSettingsStore(store) as EditorModelSettingsStore,
        config, store, agent: parseAgentSettings(config.agent) };
}
