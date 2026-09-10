import { modelMetadataSchema, type MetadataTarget, type ModelMetadata } from "./metadata.js";

const record = (value: unknown): Record<string, unknown> => value && typeof value === "object" && !Array.isArray(value) ? value as Record<string, unknown> : {};
const positive = (value: unknown) => typeof value === "number" && Number.isSafeInteger(value) && value > 0 && value <= 100_000_000 ? value : undefined;
const text = (value: unknown) => typeof value === "string" && value.trim() ? value.trim().slice(0, 512) : undefined;
const strings = (value: unknown) => Array.isArray(value) && value.every((item) => typeof item === "string" && item.length <= 64) ? value.slice(0, 32) as string[] : undefined;

export function metadataBaseUrl(target: MetadataTarget): string | undefined {
    const source = target.type === "fal" ? "https://api.fal.ai/v1"
        : target.baseUrl ?? (target.type === "openai" ? "https://api.openai.com/v1"
            : target.type === "openrouter" ? "https://openrouter.ai/api/v1" : undefined);
    if (!source) return undefined;
    const url = new URL(source);
    if (!["http:", "https:"].includes(url.protocol) || url.username || url.password || url.search || url.hash) throw new Error("Invalid model metadata base URL.");
    return url.href.replace(/\/+$/, "");
}

async function json(request: typeof fetch, url: URL, target: MetadataTarget, signal: AbortSignal): Promise<unknown> {
    const response = await request(url, {
        signal, redirect: "error",
        headers: target.apiKey ? { Authorization: `${target.type === "fal" ? "Key" : "Bearer"} ${target.apiKey}` } : {},
    });
    if (response.status === 404) return undefined;
    if (!response.ok) throw new Error("Model metadata request failed.");
    const reader = response.body?.getReader();
    if (!reader) throw new Error("Missing model metadata response.");
    const chunks: Uint8Array[] = [];
    let bytes = 0;
    try {
        while (true) {
            signal.throwIfAborted();
            const next = await reader.read();
            if (next.done) break;
            bytes += next.value.byteLength;
            if (bytes > 8 * 1024 * 1024) throw new Error("Model metadata response is too large.");
            chunks.push(next.value);
        }
    } finally { await reader.cancel().catch(() => undefined); reader.releaseLock(); }
    return JSON.parse(Buffer.concat(chunks).toString("utf8"));
}

function routerModel(value: unknown): ModelMetadata | undefined {
    const row = record(value);
    const id = text(row.id);
    if (!id) return undefined;
    const architecture = record(row.architecture);
    const top = record(row.top_provider);
    const parameters = Array.isArray(row.supported_parameters) && row.supported_parameters.every((value) => typeof value === "string") ? row.supported_parameters : undefined;
    return modelMetadataSchema.parse({
        id, name: text(row.name), inputModalities: strings(architecture.input_modalities), outputModalities: strings(architecture.output_modalities),
        chat: { contextWindow: positive(row.context_length),
            reasoning: parameters ? parameters.includes("reasoning") || parameters.includes("include_reasoning") : undefined,
            tools: parameters ? parameters.includes("tools") : undefined },
        topProvider: { contextWindow: positive(top.context_length), maxOutputTokens: positive(top.max_completion_tokens) },
    });
}

function falModel(value: unknown): ModelMetadata | undefined {
    const row = record(value);
    const id = text(row.endpoint_id);
    if (!id) return undefined;
    const info = record(row.metadata);
    const category = text(info.category)?.split("-to-");
    const knownModality = (value?: string) => value && ["text", "image", "video", "audio"].includes(value) ? [value] : undefined;
    const openapi = row.openapi;
    const operations = Object.values(record(record(openapi).paths)).map((path) => record(path).post).filter(Boolean);
    const operation = operations.length === 1 ? record(operations[0]) : {};
    const inputSchema = record(record(record(operation.requestBody).content)["application/json"]).schema;
    const responses = record(operation.responses);
    const outputSchema = record(record(record(responses["200"]).content)["application/json"]).schema;
    return modelMetadataSchema.parse({ id, name: text(info.display_name),
        inputModalities: category?.length === 2 ? knownModality(category[0]) : undefined,
        outputModalities: category?.length === 2 ? knownModality(category[1]) : undefined,
        inputSchema, outputSchema, openapi,
    });
}

export interface CatalogPage { models: ModelMetadata[]; nextCursor?: string }

export async function queryCatalog(target: MetadataTarget, request: typeof fetch, signal: AbortSignal,
    options: { modelId?: string; includeSchema?: boolean; cursor?: string } = {}): Promise<CatalogPage> {
    const baseUrl = metadataBaseUrl(target);
    if (!baseUrl) throw new Error("No model metadata endpoint.");
    const url = new URL(`${baseUrl}/models`);
    if (target.type === "openrouter") url.searchParams.set("output_modalities", "all");
    if (target.type === "fal") {
        if (options.modelId) url.searchParams.set("endpoint_id", options.modelId);
        if (options.includeSchema) url.searchParams.set("expand", "openapi-3.0");
        if (options.cursor) url.searchParams.set("cursor", options.cursor);
    }
    const body = await json(request, url, target, signal);
    if (body === undefined) return { models: [] };
    const rows = record(body)[target.type === "fal" ? "models" : "data"];
    if (!Array.isArray(rows)) throw new Error("Invalid model metadata response.");
    const models = rows.flatMap((row): ModelMetadata[] => {
        const model = target.type === "openrouter" ? routerModel(row) : target.type === "fal" ? falModel(row)
            : text(record(row).id) ? { id: text(record(row).id)! } : undefined;
        return model ? [model] : [];
    });
    return { models, ...(target.type === "fal" && text(record(body).next_cursor) ? { nextCursor: text(record(body).next_cursor) } : {}) };
}
