import { expect, it, vi } from "vitest";
import { NativeRuntime } from "@entisium/devkit/runtime/native-runtime";
import { RuntimeSession } from "@entisium/devkit/runtime/session";
import { createEditorHost } from "./server.js";
import { EncryptedCredentialStore } from "@entisium/agent/models/credential-store";

it("authenticates native commands without a page and cleans up disconnected actions", async () => {
    const runtime = new NativeRuntime("unused");
    let rejectInspection: ((error: Error) => void) | undefined;
    const inspect = vi.spyOn(runtime, "inspect").mockImplementation(() => new Promise((_done, fail) => { rejectInspection = fail; }));
    const stop = vi.spyOn(runtime, "stop").mockImplementation(async () => { rejectInspection?.(new Error("stopped")); });
    const host = createEditorHost({ credentials: new EncryptedCredentialStore(), distDirectory: ".", runtimeDirectory: ".", port: 0,
        nativeSession: new RuntimeSession(runtime),
    });
    const { host: address, port } = await host.listen();
    const url = `http://${address}:${port}/api/v1/native-runtime`;
    const request = (name: string, parameters: object, token = host.token, signal?: AbortSignal) => fetch(url, {
        method: "POST", headers: { Authorization: `Bearer ${token}`, "Content-Type": "application/json" },
        body: JSON.stringify({ name, parameters }), signal,
    });
    try {
        expect((await request("runtime_status", {}, "wrong")).status).toBe(401);
        expect(await (await request("runtime_status", {})).json()).toMatchObject({ value: { state: "stopped" } });
        expect((await request("runtime_play", { project: "other.yaml" })).ok).toBe(false);
        const controller = new AbortController();
        const pending = request("play_step", { interface: "main", action: {} }, host.token, controller.signal);
        const rejected = expect(pending).rejects.toThrow();
        await vi.waitFor(() => expect(inspect).toHaveBeenCalledOnce());
        controller.abort(); await rejected;
        await vi.waitFor(() => expect(stop).toHaveBeenCalledOnce());
    } finally {
        await host.stopRuntime(); host.server.closeAllConnections();
        await new Promise<void>((done) => host.server.close(() => done()));
    }
});
