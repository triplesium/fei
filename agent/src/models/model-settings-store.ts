import { fileURLToPath } from "node:url";
import { randomUUID } from "node:crypto";
import { mkdir, readFile, rename, unlink, writeFile } from "node:fs/promises";
import { homedir } from "node:os";
import { dirname, join, resolve } from "node:path";

export type OpenAICompatibleApi = "responses" | "chat-completions";

export interface ActiveModelSelection {
    providerId: string;
    modelId: string;
}

export interface OpenAICompatibleModelSettings {
    id: string;
    name: string;
    reasoning: boolean;
    contextWindow: number;
    maxTokens: number;
}

export interface OpenAICompatibleProviderSettings {
    id: string;
    name: string;
    baseUrl: string;
    api: OpenAICompatibleApi;
    models: OpenAICompatibleModelSettings[];
}

export interface EditorModelSettings {
    version: 2;
    active?: ActiveModelSelection;
    providers: OpenAICompatibleProviderSettings[];
}

interface LegacyCustomProviderSettings {
    providerName: string;
    baseUrl: string;
    api?: OpenAICompatibleApi;
    modelId: string;
    modelName: string;
    reasoning: boolean;
    contextWindow: number;
    maxTokens: number;
}

interface LegacyEditorModelSettings {
    version: 1;
    active?: ActiveModelSelection;
    custom?: LegacyCustomProviderSettings;
}

export interface EditorModelSettingsStore {
    read(): Promise<EditorModelSettings>;
    write(settings: EditorModelSettings): Promise<void>;
}

const deepSeekPreset: OpenAICompatibleProviderSettings = {
    id: "deepseek",
    name: "DeepSeek",
    baseUrl: "https://api.deepseek.com",
    api: "responses",
    models: [
        {
            id: "deepseek-v4-flash",
            name: "DeepSeek V4 Flash",
            reasoning: true,
            contextWindow: 1_000_000,
            maxTokens: 384_000,
        },
        {
            id: "deepseek-v4-pro",
            name: "DeepSeek V4 Pro",
            reasoning: true,
            contextWindow: 1_000_000,
            maxTokens: 384_000,
        },
    ],
};

export function defaultModelSettings(): EditorModelSettings {
    return {
        version: 2,
        active: { providerId: deepSeekPreset.id, modelId: deepSeekPreset.models[0].id },
        providers: [structuredClone(deepSeekPreset)],
    };
}

function validActive(value: unknown): value is ActiveModelSelection {
    if (!value || typeof value !== "object" || Array.isArray(value)) return false;
    const active = value as Partial<ActiveModelSelection>;
    return typeof active.providerId === "string" && typeof active.modelId === "string";
}

function validModel(value: unknown): value is OpenAICompatibleModelSettings {
    if (!value || typeof value !== "object" || Array.isArray(value)) return false;
    const model = value as Partial<OpenAICompatibleModelSettings>;
    return (
        typeof model.id === "string" &&
        typeof model.name === "string" &&
        typeof model.reasoning === "boolean" &&
        typeof model.contextWindow === "number" &&
        typeof model.maxTokens === "number"
    );
}

function validProvider(value: unknown): value is OpenAICompatibleProviderSettings {
    if (!value || typeof value !== "object" || Array.isArray(value)) return false;
    const provider = value as Partial<OpenAICompatibleProviderSettings>;
    return (
        typeof provider.id === "string" &&
        typeof provider.name === "string" &&
        typeof provider.baseUrl === "string" &&
        (provider.api === "responses" || provider.api === "chat-completions") &&
        Array.isArray(provider.models) &&
        provider.models.every(validModel)
    );
}

function migrateLegacy(settings: LegacyEditorModelSettings): EditorModelSettings {
    const next = defaultModelSettings();
    if (settings.custom) {
        next.providers.push({
            id: "custom-openai",
            name: settings.custom.providerName,
            baseUrl: settings.custom.baseUrl,
            api: settings.custom.api === "chat-completions" ? "chat-completions" : "responses",
            models: [
                {
                    id: settings.custom.modelId,
                    name: settings.custom.modelName,
                    reasoning: settings.custom.reasoning,
                    contextWindow: settings.custom.contextWindow,
                    maxTokens: settings.custom.maxTokens,
                },
            ],
        });
    }
    if (settings.active && validActive(settings.active)) next.active = structuredClone(settings.active);
    return next;
}

function parseSettings(value: unknown): EditorModelSettings {
    if (!value || typeof value !== "object" || Array.isArray(value)) {
        throw new Error("Editor model settings are invalid.");
    }
    const version = (value as { version?: unknown }).version;
    if (version === 1) return migrateLegacy(value as LegacyEditorModelSettings);
    if (version !== 2) throw new Error("Editor model settings have an unsupported version.");

    const settings = value as Partial<EditorModelSettings>;
    if (
        !Array.isArray(settings.providers) ||
        settings.providers.length === 0 ||
        !settings.providers.every(validProvider) ||
        (settings.active !== undefined && !validActive(settings.active))
    ) {
        throw new Error("The OpenAI-compatible model registry is invalid.");
    }
    return structuredClone(settings as EditorModelSettings);
}

function isUnavailablePathError(error: unknown): boolean {
    const code = (error as NodeJS.ErrnoException).code;
    return ["EACCES", "EPERM", "EROFS", "EEXIST", "ENOTDIR"].includes(code ?? "");
}

export function defaultModelSettingsPaths(): readonly string[] {
    const configuredPath = process.env.ETS_EDITOR_MODEL_SETTINGS_PATH?.trim();
    if (configuredPath) return [resolve(configuredPath)];

    const applicationData = process.env.APPDATA?.trim();
    const localApplicationData = process.env.LOCALAPPDATA?.trim();
    const roamingRoot = applicationData || join(homedir(), "AppData", "Roaming");
    const localRoot = localApplicationData || join(homedir(), "AppData", "Local");
    return Array.from(
        new Set([
            join(roamingRoot, "Entisium", "editor-model-settings.json"),
            join(localRoot, "Entisium", "editor-model-settings.json"),
            resolve(process.cwd(), ".entisium", "editor-model-settings.json"),
            fileURLToPath(new URL("../../../editor/.entisium/editor-model-settings.json", import.meta.url)),
        ]),
    );
}

export class MemoryEditorModelSettingsStore implements EditorModelSettingsStore {
    private settings: EditorModelSettings;

    constructor(settings: EditorModelSettings = defaultModelSettings()) {
        this.settings = parseSettings(settings);
    }

    async read(): Promise<EditorModelSettings> {
        return structuredClone(this.settings);
    }

    async write(settings: EditorModelSettings): Promise<void> {
        this.settings = parseSettings(settings);
    }
}

export class FileEditorModelSettingsStore implements EditorModelSettingsStore {
    private activePath: string | undefined;
    private settingsPromise: Promise<EditorModelSettings> | undefined;
    private mutationChain: Promise<void> = Promise.resolve();
    private readonly paths: readonly string[];

    constructor(paths: string | readonly string[] = defaultModelSettingsPaths()) {
        this.paths = typeof paths === "string" ? [paths] : Array.from(paths);
        if (this.paths.length === 0) throw new Error("At least one model settings path is required.");
    }

    read(): Promise<EditorModelSettings> {
        this.settingsPromise ??= this.loadFromDisk();
        return this.settingsPromise.then((settings) => structuredClone(settings));
    }

    write(settings: EditorModelSettings): Promise<void> {
        const next = parseSettings(settings);
        const result = this.mutationChain.then(async () => {
            await this.persist(next);
            this.settingsPromise = Promise.resolve(structuredClone(next));
        });
        this.mutationChain = result.catch(() => undefined);
        return result;
    }

    private async loadFromDisk(): Promise<EditorModelSettings> {
        for (const path of this.paths) {
            try {
                const settings = parseSettings(JSON.parse(await readFile(path, "utf8")));
                this.activePath = path;
                return settings;
            } catch (error) {
                if ((error as NodeJS.ErrnoException).code === "ENOENT" || isUnavailablePathError(error)) {
                    continue;
                }
                throw error;
            }
        }
        return defaultModelSettings();
    }

    private async persist(settings: EditorModelSettings): Promise<void> {
        const candidates = this.activePath ? [this.activePath] : this.paths;
        let lastError: unknown;
        for (const path of candidates) {
            const temporaryPath = `${path}.${process.pid}.${randomUUID()}.tmp`;
            try {
                await mkdir(dirname(path), { recursive: true });
                await writeFile(temporaryPath, JSON.stringify(settings, null, 2), "utf8");
                await rename(temporaryPath, path);
                this.activePath = path;
                return;
            } catch (error) {
                await unlink(temporaryPath).catch(() => undefined);
                lastError = error;
                if (this.activePath || !isUnavailablePathError(error)) throw error;
                console.warn(`[entisium editor] model settings path is unavailable: ${path}`);
            }
        }
        throw lastError ?? new Error("No writable model settings path is available.");
    }
}
