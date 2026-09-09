import { imageOptionsSchema, type ImageOptions } from "../contracts/image-generation.js";

/** Dimension arguments form one group: a tool-level group replaces YAML dimensions. */
export function imageRequestOptions(defaults: ImageOptions | undefined, input: ImageOptions, api: "openai-images" | "openrouter-images") {
    const options = { ...imageOptionsSchema.parse(defaults ?? {}) };
    if (input.size !== undefined || input.resolution !== undefined || input.aspect_ratio !== undefined) {
        delete options.size; delete options.resolution; delete options.aspect_ratio;
    }
    Object.assign(options, Object.fromEntries(Object.entries(input).filter(([, value]) => value !== undefined)));
    // Strip prompt/path before calling this helper, and validate all merged options.
    const result = imageOptionsSchema.parse(options);
    result.quality ??= "auto";
    if (result.size && !result.size.includes("x")) {
        if (result.resolution && result.resolution !== result.size) throw new Error("size and resolution conflict.");
        result.resolution = result.size as ImageOptions["resolution"];
        delete result.size;
    }
    if (result.size) {
        if (result.resolution) throw new Error("Use explicit pixel size without resolution; resolution tiers vary by provider.");
        if (result.aspect_ratio && result.aspect_ratio !== "auto") {
            const [width, height] = result.size.split("x").map(Number);
            const [x, y] = result.aspect_ratio.split(":").map(Number);
            if (width * y !== height * x) throw new Error("size and aspect_ratio conflict.");
        }
        delete result.aspect_ratio;
    } else {
        result.resolution ??= "1K";
        result.aspect_ratio ??= "1:1";
    }
    if (api === "openrouter-images") return result;
    if (result.seed !== undefined) throw new Error("seed requires openrouter-images; OpenAI Images does not support it.");
    if (!result.size) {
        const sizes: Record<string, string> = { "1:1": "1024x1024", "3:2": "1536x1024", "2:3": "1024x1536", auto: "auto" };
        if (result.resolution !== "1K" || !sizes[result.aspect_ratio!]) {
            throw new Error("This resolution/aspect_ratio cannot be mapped to OpenAI Images. Use an explicit pixel size supported by your model, or openrouter-images.");
        }
        result.size = sizes[result.aspect_ratio!];
    }
    delete result.resolution;
    delete result.aspect_ratio;
    return result;
}
