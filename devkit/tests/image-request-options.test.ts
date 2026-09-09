import { expect, it } from "vitest";
import { imageRequestOptions } from "../src/image-generation/request-options.js";

it("passes OpenRouter dimension tiers, ratio, seed and background without pixel conversion", () => {
    expect(imageRequestOptions({}, { resolution: "2K", aspect_ratio: "16:9", seed: 42, background: "transparent" }, "openrouter-images"))
        .toEqual({ resolution: "2K", aspect_ratio: "16:9", seed: 42, background: "transparent", quality: "auto" });
});
it("replaces YAML dimensions as a group and preserves unrelated defaults", () => {
    expect(imageRequestOptions({ size: "1024x1024", quality: "high" }, { resolution: "4K", aspect_ratio: "9:16" }, "openrouter-images"))
        .toEqual({ resolution: "4K", aspect_ratio: "9:16", quality: "high" });
    expect(imageRequestOptions({ resolution: "2K", aspect_ratio: "16:9" }, { size: "1024x1024" }, "openrouter-images"))
        .toEqual({ size: "1024x1024", quality: "auto" });
});
it("normalizes size shorthands and rejects contradictory dimensions", () => {
    expect(imageRequestOptions({}, { size: "2K", aspect_ratio: "16:9" }, "openrouter-images"))
        .toEqual({ resolution: "2K", aspect_ratio: "16:9", quality: "auto" });
    expect(() => imageRequestOptions({}, { size: "2K", resolution: "4K" }, "openrouter-images")).toThrow("conflict");
    expect(() => imageRequestOptions({}, { size: "1024x1024", aspect_ratio: "16:9" }, "openrouter-images")).toThrow("conflict");
    expect(() => imageRequestOptions({}, { size: "1024x1024", resolution: "2K" }, "openrouter-images")).toThrow("without resolution");
});
it("maps supported OpenAI dimensions and rejects unsupported semantics", () => {
    expect(imageRequestOptions({}, { resolution: "1K", aspect_ratio: "3:2", background: "transparent" }, "openai-images"))
        .toEqual({ size: "1536x1024", quality: "auto", background: "transparent" });
    expect(() => imageRequestOptions({}, { seed: 0 }, "openai-images")).toThrow("seed requires");
    expect(() => imageRequestOptions({}, { resolution: "4K" }, "openai-images")).toThrow("cannot be mapped");
    expect(imageRequestOptions({}, { size: "2048x2048" }, "openai-images")).toEqual({ size: "2048x2048", quality: "auto" });
});
