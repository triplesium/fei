import { expect, it, vi } from "vitest";
import { mkdtemp, rm, writeFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join } from "node:path";
import sharp from "sharp";
import { EncryptedCredentialStore } from "@entisium/agent/models/credential-store";
import { HostProjectService } from "@entisium/devkit/workspace/project-service";
import { createEditorHost } from "./server.js";

it("authenticates sprite operations and composes imported rows through the Editor host", async () => {
    const root = await mkdtemp(join(tmpdir(), "entisium-sprite-host-"));
    await writeFile(join(root, "project.yaml"), "name: Sprite Test\n");
    const project = new HostProjectService(root);
    const rectangle = await sharp({ create: { width: 4, height: 4, channels: 4, background: "red" } }).png().toBuffer();
    const source = await sharp({ create: { width: 32, height: 16, channels: 4, background: "#ff00ff" } })
        .composite([{ input: rectangle, left: 5, top: 8 }, { input: rectangle, left: 21, top: 4 }]).png().toBuffer();
    await project.writeNew("assets/source.png", source);
    const generateImage = vi.fn();
    const host = createEditorHost({ credentials: new EncryptedCredentialStore(), distDirectory: ".", runtimeDirectory: ".", port: 0,
        projectService: project, generateImage });
    const address = await host.listen();
    const post = (body: unknown, token = host.token) => fetch(`http://${address.host}:${address.port}/api/v1/sprite-animation`, {
        method: "POST", headers: { Authorization: `Bearer ${token}`, "Content-Type": "application/json" }, body: JSON.stringify(body),
    });
    try {
        expect((await post({}, "wrong")).status).toBe(401);
        expect((await post({ operation: "compose", run: "../bad" })).ok).toBe(false);
        const prepared = await post({ operation: "prepare", run: "assets/run", request: {
            reference: "assets/source.png", description: "Red hero", states: [{ name: "jump", action: "Jump", poses: ["Down", "Up"] }],
        } });
        expect(prepared.ok).toBe(true);
        const composed = await post({ operation: "compose", run: "assets/run", selections: [{ state: "jump", source: "assets/source.png" }] });
        expect(composed.ok).toBe(true);
        const result = await composed.json();
        expect(result.paths[0]).toMatch(/animation\.json$/);
        expect(await project.exists(result.paths[1])).toBe(true);
        expect(generateImage).not.toHaveBeenCalled();
    } finally {
        host.server.closeAllConnections();
        await new Promise<void>(done => host.server.close(() => done()));
        project.dispose();
        await rm(root, { recursive: true, force: true });
    }
});
