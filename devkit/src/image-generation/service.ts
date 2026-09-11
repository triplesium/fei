import type { FalQueueOptions } from "../providers/fal/queue.js";
import { generateFalImage } from "../providers/fal/images.js";
import { falGptImageModel } from "../providers/fal/gpt-image.js";
import { imageRequestOptions, type ImageGenerationApi } from "./request-options.js";
import { imageGenerationSchema, type ImageGenerationResult, type ImageOptions } from "../contracts/image-generation.js";
import { HostProjectService } from "../workspace/project-service.js";
import type { ResolvedModelMetadata } from "../models/metadata.js";

export interface ImageGenerationOptions {
    model: string;
    baseUrl?: string;
    api?: ImageGenerationApi;
    timeoutMs?: number;
    defaults?: ImageOptions;
    resolveApiKey: (signal?: AbortSignal) => Promise<string | undefined>;
    resolveMetadata?: (model: string, apiKey: string, signal?: AbortSignal) => Promise<ResolvedModelMetadata>;
    fetch?: typeof fetch;
    createFalClient?: FalQueueOptions["createFalClient"];
}

/** Host-only service. Credentials and provider configuration never enter tool arguments. */
export class ImageGenerationService {
    constructor(private readonly project: HostProjectService, private readonly options: ImageGenerationOptions) {}

    async generate(parameters: unknown, signal?: AbortSignal): Promise<ImageGenerationResult> {
        const input = imageGenerationSchema.parse(parameters);
        const { prompt, path: _path, references: referencePaths, ...options } = input;
        const requestOptions = imageRequestOptions(this.options.defaults, options, this.options.api ?? "openai-images", this.options.model);
        const api = this.options.api ?? "openai-images";
        const endpoint = api === "fal-images" ? undefined : imageEndpoint(this.options.baseUrl, api);
        const requestSignal = signal
            ? AbortSignal.any([signal, AbortSignal.timeout(this.options.timeoutMs ?? 10 * 60_000)])
            : AbortSignal.timeout(this.options.timeoutMs ?? 10 * 60_000);
        requestSignal.throwIfAborted();
        // Pin this operation to the project that was open when generation started.
        const project = new HostProjectService(await this.project.workspaceRoot());
        try {
            if (await project.exists(input.path)) throw new Error(`Image already exists: ${input.path}`);
            const references: Buffer[] = [];
            for (const path of referencePaths ?? []) {
                const image = await project.read(path);
                if (!image || image.length > 10 * 1024 * 1024 || image.length < 33 ||
                    !image.subarray(0, 8).equals(Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]))) {
                    throw new Error(`Reference must be a project PNG of at most 10 MiB: ${path}`);
                }
                references.push(image);
            }
            const apiKey = await this.options.resolveApiKey(requestSignal);
            requestSignal.throwIfAborted();
            if (!apiKey?.trim()) throw new Error("Image generation requires an API key for the selected provider. Configure providers.<provider>.apiKey in config.yaml or model settings.");
            const outputModel = references.length && api === "fal-images" && this.options.model === falGptImageModel
                ? this.options.model.replace(/\/text-to-image$/, "/edit") : this.options.model;
            const metadata = await this.options.resolveMetadata?.(outputModel, apiKey, requestSignal);
            requestSignal.throwIfAborted();
            if (metadata?.model.outputModalities?.length && !metadata.model.outputModalities.includes("image")) {
                throw new Error("The selected model reports no image output capability.");
            }
            if (references.length && metadata?.model.inputModalities?.length && !metadata.model.inputModalities.includes("image")) {
                throw new Error("The selected model reports no reference image input capability.");
            }
            const output = api === "fal-images"
                ? await generateFalImage({
                    model: this.options.model, prompt, requestOptions, apiKey, signal: requestSignal,
                    fetch: this.options.fetch, createFalClient: this.options.createFalClient,
                    references: references.map(image => `data:image/png;base64,${image.toString("base64")}`),
                })
                : await this.generateWithOpenAi(endpoint!, prompt, requestOptions, apiKey, requestSignal, references, api);
            const mediaType = output.mediaType;
            if (mediaType !== undefined && mediaType !== "image/png") throw new Error("Image provider did not return a PNG image.");
            const encoded = output.encoded;
            if (typeof encoded !== "string" || encoded.length > 48 * 1024 * 1024 ||
                encoded.length % 4 !== 0 || !/^[A-Za-z0-9+/]*={0,2}$/.test(encoded)) {
                throw new Error("Image provider returned an invalid or oversized image.");
            }
            const image = Buffer.from(encoded, "base64");
            if (image.length < 33 || !image.subarray(0, 8).equals(Buffer.from([137, 80, 78, 71, 13, 10, 26, 10])) ||
                image.toString("ascii", 12, 16) !== "IHDR") throw new Error("Image provider did not return a PNG image.");
            const width = image.readUInt32BE(16);
            const height = image.readUInt32BE(20);
            if (!width || !height) throw new Error("Image provider returned invalid image dimensions.");
            requestSignal.throwIfAborted();
            await project.writeNew(input.path, image);
            return { path: input.path, mimeType: "image/png", width, height, bytes: image.length, model: outputModel };
        } finally { project.dispose(); }
    }

    private async generateWithOpenAi(endpoint: string, prompt: string, requestOptions: object, apiKey: string, signal: AbortSignal,
        references: Buffer[], api: ImageGenerationApi) {
        const payload = { model: this.options.model, prompt, ...requestOptions, n: 1, output_format: "png" };
        let body: string | FormData;
        const headers: Record<string, string> = { Authorization: `Bearer ${apiKey}` };
        if (references.length && api === "openai-images") {
            endpoint = endpoint.replace(/\/generations$/, "/edits");
            const form = new FormData();
            for (const [key, value] of Object.entries(payload)) form.append(key, String(value));
            for (const [index, image] of references.entries()) form.append("image[]", new Blob([new Uint8Array(image)], { type: "image/png" }), `reference-${index}.png`);
            body = form;
        } else {
            headers["Content-Type"] = "application/json";
            body = JSON.stringify({ ...payload, ...(references.length ? {
                input_references: references.map(image => ({ type: "image_url", image_url: { url: `data:image/png;base64,${image.toString("base64")}` } })),
            } : {}) });
        }
        const response = await (this.options.fetch ?? fetch)(endpoint, {
            method: "POST", signal,
            headers, body,
        });
        // Do not echo upstream bodies: they may contain credentials or huge payloads.
        if (!response.ok) throw new Error(`Image generation failed (HTTP ${response.status}). Check API access, quota and image model configuration. The request was not retried.`);
        const output = await response.json() as { data?: { b64_json?: unknown; media_type?: unknown }[] };
        return { encoded: output?.data?.[0]?.b64_json, mediaType: output?.data?.[0]?.media_type };
    }
}

function imageEndpoint(configured: string | undefined, api: Exclude<ImageGenerationApi, "fal-images">) {
    let baseUrl: URL;
    try { baseUrl = new URL(configured ?? (api === "openrouter-images" ? "https://openrouter.ai/api/v1" : "https://api.openai.com/v1")); }
    catch { throw new Error("Image generation baseUrl must be a valid HTTP or HTTPS URL."); }
    if (!["https:", "http:"].includes(baseUrl.protocol) || baseUrl.username || baseUrl.password || baseUrl.search || baseUrl.hash) {
        throw new Error("Image generation baseUrl must use HTTP or HTTPS without credentials, query or fragment.");
    }
    const route = api === "openrouter-images" ? "/images" : "/images/generations";
    return `${baseUrl.href.replace(/\/+$/, "")}${route}`;
}
