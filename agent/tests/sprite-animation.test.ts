import { expect, it, vi } from "vitest";
import { createSpriteAnimationTools } from "../src/tools/sprite-animation.js";

it("exposes a browser-safe staged tool, validates inputs and forwards cancellation", async () => {
    const invoke = vi.fn(async () => ({ operation: "compose" as const, run: "assets/run", paths: ["assets/run/exports/id/animation.json"] }));
    const [tool] = createSpriteAnimationTools(invoke);
    expect(tool.name).toBe("sprite_animation");
    const signal = new AbortController().signal;
    const result = await tool.execute("call", { operation: "compose", run: "assets/run" }, signal);
    expect(invoke).toHaveBeenCalledWith({ operation: "compose", run: "assets/run", tolerance: 40, despill: true, align: true, selections: [] }, signal);
    expect(result.details.paths).toHaveLength(1);
    await expect(tool.execute("call", { operation: "compose", run: "../bad" }, signal)).rejects.toThrow();
    await expect(tool.execute("call", { operation: "compose", run: "assets/run" }, AbortSignal.abort())).rejects.toThrow();
    expect(invoke).toHaveBeenCalledOnce();
});
