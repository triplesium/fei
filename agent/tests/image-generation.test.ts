import { expect, it, vi } from "vitest";
import { createImageGenerationTools } from "../src/tools/image-generation.js";

it("validates tool arguments and returns asset metadata without embedding image bytes", async () => {
    const result = { path: "assets/icon.png", mimeType: "image/png" as const, width: 1024, height: 1024, bytes: 100, model: "test" };
    const invoke = vi.fn(async () => result);
    const [tool] = createImageGenerationTools(invoke);
    const signal = new AbortController().signal;
    expect(await tool.execute("test", { prompt: "An icon", path: result.path }, signal)).toEqual({
        content: [{ type: "text", text: JSON.stringify(result) }], details: result,
    });
    expect(invoke).toHaveBeenCalledWith({ prompt: "An icon", path: result.path }, signal);
    await expect(tool.execute("test", { prompt: "An icon", path: "../icon.png" }, signal)).rejects.toThrow();
    expect(invoke).toHaveBeenCalledOnce();
});
