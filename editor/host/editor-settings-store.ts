import { randomUUID } from "node:crypto";
import { mkdir, readFile, rename, unlink, writeFile } from "node:fs/promises";
import { homedir } from "node:os";
import { dirname, join, resolve } from "node:path";

export type AgentDensity = "compact" | "comfortable";

export interface EditorSettings {
    version: 1;
    appearance: {
        agentDensity: AgentDensity;
    };
}

export interface EditorSettingsStore {
    read(): Promise<EditorSettings>;
    write(settings: EditorSettings): Promise<void>;
}

export const defaultEditorSettings: EditorSettings = {
    version: 1,
    appearance: {
        agentDensity: "compact",
    },
};

function parseSettings(value: unknown): EditorSettings {
    if (!value || typeof value !== "object" || Array.isArray(value)) {
        throw new Error("Editor settings are invalid.");
    }
    const settings = value as Partial<EditorSettings>;
    if (
        settings.version !== 1 ||
        !settings.appearance ||
        (settings.appearance.agentDensity !== "compact" &&
            settings.appearance.agentDensity !== "comfortable")
    ) {
        throw new Error("Editor settings have an unsupported format.");
    }
    return structuredClone(settings as EditorSettings);
}

function isUnavailablePathError(error: unknown): boolean {
    const code = (error as NodeJS.ErrnoException).code;
    return ["EACCES", "EPERM", "EROFS", "EEXIST", "ENOTDIR"].includes(code ?? "");
}

export function defaultEditorSettingsPaths(): readonly string[] {
    const configuredPath = process.env.FEI_EDITOR_SETTINGS_PATH?.trim();
    if (configuredPath) return [resolve(configuredPath)];

    const applicationData = process.env.APPDATA?.trim();
    const localApplicationData = process.env.LOCALAPPDATA?.trim();
    const roamingRoot = applicationData || join(homedir(), "AppData", "Roaming");
    const localRoot = localApplicationData || join(homedir(), "AppData", "Local");
    return Array.from(
        new Set([
            join(roamingRoot, "Fei", "editor-settings.json"),
            join(localRoot, "Fei", "editor-settings.json"),
            resolve(process.cwd(), ".fei", "editor-settings.json"),
        ]),
    );
}

export class MemoryEditorSettingsStore implements EditorSettingsStore {
    constructor(private settings: EditorSettings = defaultEditorSettings) {}

    async read(): Promise<EditorSettings> {
        return structuredClone(this.settings);
    }

    async write(settings: EditorSettings): Promise<void> {
        this.settings = parseSettings(settings);
    }
}

export class FileEditorSettingsStore implements EditorSettingsStore {
    private activePath: string | undefined;
    private settingsPromise: Promise<EditorSettings> | undefined;
    private mutationChain: Promise<void> = Promise.resolve();
    private readonly paths: readonly string[];

    constructor(paths: string | readonly string[] = defaultEditorSettingsPaths()) {
        this.paths = typeof paths === "string" ? [paths] : Array.from(paths);
        if (this.paths.length === 0) throw new Error("At least one Editor settings path is required.");
    }

    read(): Promise<EditorSettings> {
        this.settingsPromise ??= this.loadFromDisk();
        return this.settingsPromise.then((settings) => structuredClone(settings));
    }

    write(settings: EditorSettings): Promise<void> {
        const next = parseSettings(settings);
        const result = this.mutationChain.then(async () => {
            await this.persist(next);
            this.settingsPromise = Promise.resolve(structuredClone(next));
        });
        this.mutationChain = result.catch(() => undefined);
        return result;
    }

    private async loadFromDisk(): Promise<EditorSettings> {
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
        return structuredClone(defaultEditorSettings);
    }

    private async persist(settings: EditorSettings): Promise<void> {
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
                console.warn(`[fei editor] settings path is unavailable: ${path}`);
            }
        }
        throw lastError ?? new Error("No writable Editor settings path is available.");
    }
}
