import { randomUUID } from "node:crypto";
import { mkdir, readFile, rename, unlink } from "node:fs/promises";
import { writeFile } from "node:fs/promises";
import { homedir } from "node:os";
import { dirname, isAbsolute, join, resolve } from "node:path";
import { isMap, parseDocument, type Document } from "yaml";
import { parseConfig, type EntisiumConfig } from "./config.js";

export function defaultConfigPath(): string {
    const configured = process.env.ETS_CONFIG_PATH?.trim();
    if (configured) return resolve(configured);
    const directory = process.platform === "win32"
        ? join(process.env.APPDATA?.trim() || join(homedir(), "AppData", "Roaming"), "Entisium")
        : join(process.env.XDG_CONFIG_HOME?.trim() || join(homedir(), ".config"), "entisium");
    return join(directory, "config.yaml");
}

/** Select exactly one configuration; an invalid higher-priority file never falls back. */
export async function loadConfig(path?: string, projectDirectory = process.cwd()) {
    const explicit = path ?? process.env.ETS_CONFIG_PATH?.trim();
    if (explicit !== undefined && explicit !== "") {
        const store = new YamlConfigStore(explicit);
        const config = await store.readOptional();
        if (!config) throw new Error("The explicitly selected config.yaml does not exist.");
        return { store, config };
    }
    const local = new YamlConfigStore(join(projectDirectory, ".entisium", "config.yaml"));
    const projectConfig = await local.readOptional();
    if (projectConfig) return { store: local, config: projectConfig };
    const store = new YamlConfigStore(defaultConfigPath());
    return { store, config: await store.readOptional() };
}

/** One shared store per host. Updates read the latest document and preserve untouched sections. */
export class YamlConfigStore {
    readonly path: string;
    private mutations: Promise<unknown> = Promise.resolve();
    constructor(path = defaultConfigPath()) { this.path = resolve(path); }

    private async document(): Promise<Document> {
        const source = await readFile(this.path, "utf8");
        if (source.length > 1024 * 1024) throw new Error("config.yaml exceeds 1 MiB.");
        const document = parseDocument(source, { prettyErrors: false, uniqueKeys: true });
        if (document.errors.length || !isMap(document.contents)) throw new Error("Invalid config.yaml syntax; expected a mapping with unique keys.");
        return document;
    }

    async readOptional(): Promise<EntisiumConfig | undefined> {
        try { return await this.read(); }
        catch (error) {
            if ((error as NodeJS.ErrnoException).code === "ENOENT") return undefined;
            throw error;
        }
    }

    async read(): Promise<EntisiumConfig> {
        const document = await this.document();
        try { return parseConfig(document.toJS({ maxAliasCount: 50 })); }
        catch (error) {
            if (error instanceof Error && error.message.startsWith("Invalid config.yaml fields:")) throw error;
            throw new Error("Invalid config.yaml structure.");
        }
    }

    update(edit: (config: EntisiumConfig) => void | Promise<void>): Promise<void> {
        const operation = this.mutations.then(async () => {
            const document = await this.document();
            let previous: EntisiumConfig;
            try { previous = parseConfig(document.toJS({ maxAliasCount: 50 })); }
            catch { throw new Error("Invalid config.yaml structure."); }
            const next = structuredClone(previous);
            await edit(next);
            const validated = parseConfig(next);
            for (const key of Object.keys(validated) as (keyof EntisiumConfig)[]) {
                if (JSON.stringify(previous[key]) !== JSON.stringify(validated[key])) document.set(key, validated[key]);
            }
            const temporary = `${this.path}.${randomUUID()}.tmp`;
            try {
                await mkdir(dirname(this.path), { recursive: true });
                await writeFile(temporary, document.toString(), { flag: "wx", mode: 0o600 });
                await rename(temporary, this.path);
            } finally { await unlink(temporary).catch(() => undefined); }
        });
        this.mutations = operation.catch(() => undefined);
        return operation;
    }

    resolvePath(value: string): string {
        return isAbsolute(value) ? value : resolve(dirname(this.path), value);
    }
}
