import { expect, it, vi } from "vitest";
import { EncryptedCredentialStore } from "@entisium/agent/models/credential-store";
import { createEditorHost } from "./server.js";

it("authenticates generation, validates inputs, returns metadata and cancels disconnected requests", async () => {
    const generate = vi.fn(async (_input: unknown, _signal?: AbortSignal) => ({ path: "assets/icon.png", mimeType: "image/png" as const, width: 1, height: 1, bytes: 68, model: "test" }));
    const host = createEditorHost({ credentials: new EncryptedCredentialStore(), distDirectory: ".", runtimeDirectory: ".", port: 0, generateImage: generate });
    const address = await host.listen();
    const url = `http://${address.host}:${address.port}/api/v1/image-generation`;
    const request = (token = host.token, path = "assets/icon.png", signal?: AbortSignal) => fetch(url, {
        method: "POST", headers: { Authorization: `Bearer ${token}`, "Content-Type": "application/json" },
        body: JSON.stringify({ prompt: "An icon", path }), signal,
    });
    try {
        expect((await request("wrong")).status).toBe(401);
        expect((await request(host.token, "../icon.png")).ok).toBe(false);
        expect(generate).not.toHaveBeenCalled();
        expect(await (await request()).json()).toMatchObject({ path: "assets/icon.png", width: 1 });
        let runningSignal: AbortSignal | undefined;
        generate.mockImplementationOnce((_input, signal) => new Promise((_resolve, reject) => {
            runningSignal = signal;
            signal!.addEventListener("abort", () => reject(new Error("cancelled")), { once: true });
        }));
        const controller = new AbortController();
        const pending = request(host.token, "assets/another.png", controller.signal);
        const rejected = expect(pending).rejects.toThrow();
        await vi.waitFor(() => expect(runningSignal).toBeDefined());
        controller.abort();
        await rejected;
        await vi.waitFor(() => expect(runningSignal!.aborted).toBe(true));
    } finally {
        host.server.closeAllConnections();
        await new Promise<void>((done) => host.server.close(() => done()));
    }
});
