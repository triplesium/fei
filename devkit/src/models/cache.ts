import { mkdir, readFile, rename, stat, unlink, writeFile } from "node:fs/promises";
import { randomUUID } from "node:crypto";
import { homedir } from "node:os";
import { join } from "node:path";
import { z } from "zod/v4";
import { modelMetadataSchema } from "./metadata.js";

export const cacheEntrySchema = z.object({
    fetchedAt: z.number().finite().nonnegative(),
    models: z.array(modelMetadataSchema).max(20_000), nextCursor: z.string().optional(),
});
export type MetadataCacheEntry = z.infer<typeof cacheEntrySchema>;

export function defaultMetadataCacheDirectory() {
    return process.platform === "win32" ? join(process.env.LOCALAPPDATA || join(homedir(), "AppData", "Local"), "Entisium", "model-metadata")
        : join(process.env.XDG_CACHE_HOME || join(homedir(), ".cache"), "entisium", "model-metadata");
}

export class MetadataCache {
    private readonly entries = new Map<string, MetadataCacheEntry>();
    constructor(private readonly directory: string | null = defaultMetadataCacheDirectory()) {}
    async read(key: string): Promise<MetadataCacheEntry | undefined> {
        if (!/^[a-f0-9]{64}$/.test(key)) throw new Error("Invalid model cache key.");
        const memory = this.entries.get(key);
        if (memory) return structuredClone(memory);
        if (!this.directory) return undefined;
        try {
            if ((await stat(join(this.directory, `${key}.json`))).size > 8 * 1024 * 1024) return undefined;
            const bytes = await readFile(join(this.directory, `${key}.json`));
            if (bytes.length > 8 * 1024 * 1024) return undefined;
            const entry = cacheEntrySchema.parse(JSON.parse(bytes.toString("utf8")));
            this.entries.set(key, entry);
            return structuredClone(entry);
        } catch { return undefined; }
    }
    async write(key: string, entry: MetadataCacheEntry): Promise<void> {
        if (!/^[a-f0-9]{64}$/.test(key)) throw new Error("Invalid model cache key.");
        this.entries.set(key, structuredClone(entry));
        if (!this.directory) return;
        const temporary = join(this.directory, `${key}.${randomUUID()}.tmp`);
        try {
            await mkdir(this.directory, { recursive: true });
            await writeFile(temporary, JSON.stringify(entry), { flag: "wx", mode: 0o600 });
            await rename(temporary, join(this.directory, `${key}.json`));
        } catch { /* Cache persistence is best-effort; keep the in-memory entry. */ }
        finally { await unlink(temporary).catch(() => undefined); }
    }
}
