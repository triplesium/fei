import { createHash } from "node:crypto";
import { builtinMetadata } from "./builtins.js";
import { metadataBaseUrl, queryCatalog, type CatalogPage } from "./adapters.js";
import { MetadataCache, type MetadataCacheEntry } from "./cache.js";
import { mergeMetadata, type MetadataTarget, type ModelMetadata, type QueryStatus, type ResolvedModelMetadata } from "./metadata.js";

export interface MetadataServiceOptions {
    fetch?: typeof fetch;
    cacheDirectory?: string | null;
    ttlMs?: number;
    timeoutMs?: number;
    now?: () => number;
}
interface QueryOptions { force?: boolean; includeSchema?: boolean; cursor?: string; signal?: AbortSignal }
export function fallbackModelMetadata(target: MetadataTarget, id: string, override?: ModelMetadata): ResolvedModelMetadata {
    return { ...mergeMetadata(id, [{ model: builtinMetadata(target, id), source: "builtin" }, { model: override, source: "override" }]),
        status: "unavailable", stale: false };
}
export function catalogModelMetadata(target: MetadataTarget, model: ModelMetadata, fetchedAt?: number, override?: ModelMetadata): ResolvedModelMetadata {
    return { ...mergeMetadata(model.id, [
        { model: builtinMetadata(target, model.id), source: "builtin" },
        { model, source: "remote", fetchedAt }, { model: override, source: "override" },
    ]), status: "found", stale: false };
}
interface CachedCatalog { entry?: MetadataCacheEntry; status: QueryStatus; stale: boolean }

/** One host service shared by model discovery, chat and media generation. No YAML writes. */
export class ModelMetadataService {
    private readonly cache: MetadataCache;
    private readonly inFlight = new Map<string, Promise<CachedCatalog>>();
    private readonly retryAfter = new Map<string, number>();
    private readonly now: () => number;
    constructor(private readonly options: MetadataServiceOptions = {}) {
        this.cache = new MetadataCache(options.cacheDirectory);
        this.now = options.now ?? Date.now;
    }

    async getModel(target: MetadataTarget, id: string, options: QueryOptions & { override?: ModelMetadata } = {}): Promise<ResolvedModelMetadata> {
        const remote = await this.catalog(target, { ...options, modelId: target.type === "fal" ? id : undefined });
        const model = remote.entry?.models.find((model) => model.id === id);
        const merged = mergeMetadata(id, [
            { model: builtinMetadata(target, id), source: "builtin" },
            { model, source: "remote", fetchedAt: remote.entry?.fetchedAt },
            { model: options.override, source: "override" },
        ]);
        return { ...merged, status: remote.status === "found" && !model ? "not-found" : remote.status, stale: remote.stale };
    }

    async listModels(target: MetadataTarget, options: QueryOptions = {}): Promise<CatalogPage & { status: QueryStatus; stale: boolean; fetchedAt?: number }> {
        const remote = await this.catalog(target, options);
        return { models: structuredClone(remote.entry?.models ?? []), nextCursor: remote.entry?.nextCursor,
            status: remote.status, stale: remote.stale, fetchedAt: remote.entry?.fetchedAt };
    }

    private async catalog(target: MetadataTarget, options: QueryOptions & { modelId?: string }): Promise<CachedCatalog> {
        options.signal?.throwIfAborted();
        if (target.type === "openai-compatible") return { status: "unsupported", stale: false };
        if (target.type === "openai" && !target.apiKey) return { status: "unavailable", stale: false };
        const key = createHash("sha256").update(JSON.stringify([
            target.providerId, target.type, metadataBaseUrl(target), target.apiKey ?? null,
            options.modelId ?? null, options.includeSchema ?? false, options.cursor ?? null,
        ])).digest("hex");
        const cached = await this.cache.read(key);
        options.signal?.throwIfAborted();
        const age = cached ? this.now() - cached.fetchedAt : Infinity;
        const fresh = cached && age >= 0 && age < (this.options.ttlMs ?? 60 * 60_000);
        if (fresh && !options.force) return { entry: cached, status: "found", stale: false };
        if (!options.force && this.now() < (this.retryAfter.get(key) ?? 0)) return { entry: cached, status: "unavailable", stale: Boolean(cached) };
        let pending = this.inFlight.get(key);
        if (!pending) {
            pending = this.refresh(key, target, options, cached).finally(() => { this.inFlight.delete(key); });
            this.inFlight.set(key, pending);
        }
        // Stale-while-revalidate: a refresh never changes the returned task snapshot.
        if (cached && !options.force) return { entry: cached, status: "found", stale: true };
        return waitForCaller(pending, options.signal);
    }

    private async refresh(key: string, target: MetadataTarget, options: QueryOptions & { modelId?: string }, cached?: MetadataCacheEntry): Promise<CachedCatalog> {
        // Individual callers can cancel their wait without cancelling another caller's refresh.
        const signal = AbortSignal.timeout(this.options.timeoutMs ?? 5000);
        try {
            const page = await queryCatalog(target, this.options.fetch ?? fetch, signal, options);
            const entry = { ...page, fetchedAt: this.now() };
            await this.cache.write(key, entry);
            this.retryAfter.delete(key);
            return { entry, status: "found", stale: false };
        } catch {
            this.retryAfter.set(key, this.now() + 30_000);
            return { entry: cached, status: "unavailable", stale: Boolean(cached) };
        }
    }
}

function waitForCaller<T>(pending: Promise<T>, signal?: AbortSignal): Promise<T> {
    if (!signal) return pending;
    signal.throwIfAborted();
    return new Promise((resolve, reject) => {
        const abort = () => reject(signal.reason);
        signal.addEventListener("abort", abort, { once: true });
        pending.then(resolve, reject).finally(() => signal.removeEventListener("abort", abort));
    });
}
