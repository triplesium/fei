import { expect, it, vi } from "vitest";
import { NativeRuntime } from "../src/runtime/native-runtime.js";
import { RuntimeSession } from "../src/runtime/session.js";

it("rejects malformed actions before dispatch and keeps the project binding", async () => {
    const runtime = new NativeRuntime("unused");
    const start = vi.spyOn(runtime, "start").mockResolvedValue(runtime.status());
    const inspect = vi.spyOn(runtime, "inspect");
    const session = new RuntimeSession(runtime, async () => "project.yaml");
    await expect(session.invoke("play_segment", { interface: "main", source: "x", max_ticks: 601 })).rejects.toThrow();
    expect(inspect).not.toHaveBeenCalled();
    await expect(session.invoke("runtime_play", { project: "other.yaml" })).rejects.toThrow("bound");
    expect(start).not.toHaveBeenCalled();
    await session.invoke("runtime_play", {});
    expect(start).toHaveBeenCalledWith("project.yaml");
});

it("stops and awaits cleanup when an inspection is cancelled", async () => {
    const runtime = new NativeRuntime("unused");
    let reject!: (error: Error) => void;
    vi.spyOn(runtime, "inspect").mockImplementation(() => new Promise((_resolve, fail) => { reject = fail; }));
    let cleaned = false;
    const stop = vi.spyOn(runtime, "stop").mockImplementation(async () => {
        reject(new Error("stopped")); await Promise.resolve(); cleaned = true;
    });
    const controller = new AbortController();
    const operation = new RuntimeSession(runtime).invoke("play_step", { interface: "main", action: {} }, controller.signal);
    const rejected = expect(operation).rejects.toThrow("stopped");
    controller.abort(); await rejected;
    expect(stop).toHaveBeenCalledOnce(); expect(cleaned).toBe(true);
});

it("does not start a game if cancellation arrives while resolving the project", async () => {
    const runtime = new NativeRuntime("unused");
    const start = vi.spyOn(runtime, "start");
    vi.spyOn(runtime, "stop").mockResolvedValue();
    let resolveProject!: (path: string) => void;
    const session = new RuntimeSession(runtime, () => new Promise((resolve) => { resolveProject = resolve; }));
    const controller = new AbortController();
    const pending = session.invoke("runtime_play", {}, controller.signal);
    const rejected = expect(pending).rejects.toThrow();
    controller.abort(); resolveProject("project.yaml"); await rejected;
    expect(start).not.toHaveBeenCalled();
});
