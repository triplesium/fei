import { imageRequestOptions } from "./request-options.js";
import { imageGenerationSchema, type ImageGenerationResult, type ImageOptions } from "../contracts/image-generation.js";
import { HostProjectService } from "../workspace/project-service.js";

export interface ImageGenerationOptions {
    model: string;
    baseUrl?: string;
    api?: "openai-images" | "openrouter-images";
    timeoutMs?: number;
    defaults?: ImageOptions;
    resolveApiKey: (signal?: AbortSignal) => Promise<string | undefined>;
    fetch?: typeof fetch;
}

/** Host-only service. Credentials and provider configuration never enter tool arguments. */
export class ImageGenerationService {
    constructor(private readonly project: HostProjectService, private readonly options: ImageGenerationOptions) {}

    async generate(parameters: unknown, signal?: AbortSignal): Promise<ImageGenerationResult> {
        const input = imageGenerationSchema.parse(parameters);
        const { prompt, path: _path, ...options } = input;
        const requestOptions = imageRequestOptions(this.options.defaults, options, this.options.api ?? "openai-images");
        let baseUrl: URL;
        try { baseUrl = new URL(this.options.baseUrl ?? (this.options.api === "openrouter-images" ? "https://openrouter.ai/api/v1" : "https://api.openai.com/v1")); }
        catch { throw new Error("Image generation baseUrl must be a valid HTTP or HTTPS URL."); }
        if (!["https:", "http:"].includes(baseUrl.protocol) || baseUrl.username || baseUrl.password || baseUrl.search || baseUrl.hash) {
            throw new Error("Image generation baseUrl must use HTTP or HTTPS without credentials, query or fragment.");
        }
        const route = this.options.api === "openrouter-images" ? "/images" : "/images/generations";
        const endpoint = `${baseUrl.href.replace(/\/+$/, "")}${route}`;
        const requestSignal = signal
            ? AbortSignal.any([signal, AbortSignal.timeout(this.options.timeoutMs ?? 10 * 60_000)])
            : AbortSignal.timeout(this.options.timeoutMs ?? 10 * 60_000);
        requestSignal.throwIfAborted();
        // Pin this operation to the project that was open when generation started.
        const project = new HostProjectService(await this.project.workspaceRoot());
        try {
            if (await project.exists(input.path)) throw new Error(`Image already exists: ${input.path}`);
            const apiKey = await this.options.resolveApiKey(requestSignal);
            requestSignal.throwIfAborted();
            if (!apiKey?.trim()) throw new Error("Image generation requires an API key for the selected provider. Configure providers.<provider>.apiKey in config.yaml or model settings.");
            const response = await (this.options.fetch ?? fetch)(endpoint, {
                method: "POST", signal: requestSignal,
                headers: { Authorization: `Bearer ${apiKey}`, "Content-Type": "application/json" },
                body: JSON.stringify({ model: this.options.model, prompt, ...requestOptions, n: 1, output_format: "png" }),
            });
            // Do not echo upstream bodies: they may contain credentials or huge payloads.
            if (!response.ok) throw new Error(`Image generation failed (HTTP ${response.status}). Check API access, quota and image model configuration. The request was not retried.`);
            const body = await response.json() as { data?: { b64_json?: unknown; media_type?: unknown }[] };
            const mediaType = body?.data?.[0]?.media_type;
            if (mediaType !== undefined && mediaType !== "image/png") throw new Error("Image provider did not return a PNG image.");
            const encoded = body?.data?.[0]?.b64_json;
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
            return { path: input.path, mimeType: "image/png", width, height, bytes: image.length, model: this.options.model };
        } finally { project.dispose(); }
    }
}
