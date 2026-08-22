import { mkdir, readFile, rm, mkdtemp, writeFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { afterEach, describe, expect, it } from "vitest";
import {
    EncryptedCredentialStore,
    type SecretProtector,
} from "./credential-store.js";
import { createEditorHost } from "./server.js";

const temporaryDirectories: string[] = [];

const testProtector: SecretProtector = {
    protect: async (plaintext) => Buffer.from(plaintext, "utf8").toString("base64"),
    unprotect: async (ciphertext) => Buffer.from(ciphertext, "base64").toString("utf8"),
};

afterEach(async () => {
    await Promise.all(
        temporaryDirectories.splice(0).map((directory) =>
            rm(directory, { recursive: true, force: true }),
        ),
    );
});

describe("Editor Host", () => {
    it("bootstraps a session and stores credentials without returning the key", async () => {
        const directory = await mkdtemp(join(tmpdir(), "fei-editor-host-"));
        temporaryDirectories.push(directory);
        const distDirectory = join(directory, "dist");
        const credentialPath = join(directory, "credentials.json");
        await mkdir(distDirectory);
        await writeFile(join(distDirectory, "index.html"), "<p>Fei Editor</p>", "utf8");
        const host = createEditorHost({
            credentials: new EncryptedCredentialStore(credentialPath, testProtector),
            distDirectory,
            runtimeDirectory: distDirectory,
            port: 0,
        });
        const address = await host.listen();
        const baseUrl = `http://${address.host}:${address.port}`;

        try {
            const initial = await fetch(`${baseUrl}/api/v1/bootstrap`).then((response) => response.json());
            expect(initial).toMatchObject({
                version: 1,
                provider: { id: "deepseek", configured: false },
            });
            expect(initial).not.toHaveProperty("provider.apiKey");

            const saved = await fetch(`${baseUrl}/api/v1/credentials/deepseek`, {
                method: "PUT",
                headers: {
                    Authorization: `Bearer ${initial.token}`,
                    "Content-Type": "application/json",
                },
                body: JSON.stringify({ apiKey: "test-deepseek-secret" }),
            });
            expect(saved.status).toBe(200);
            expect(await readFile(credentialPath, "utf8")).not.toContain("test-deepseek-secret");

            const configured = await fetch(`${baseUrl}/api/v1/bootstrap`).then((response) => response.json());
            expect(configured.provider.configured).toBe(true);
            expect(JSON.stringify(configured)).not.toContain("test-deepseek-secret");
        } finally {
            await new Promise<void>((resolveClose) => host.server.close(() => resolveClose()));
        }
    });

    it("serves only the known WebAssembly runtime artifacts", async () => {
        const directory = await mkdtemp(join(tmpdir(), "fei-editor-runtime-"));
        temporaryDirectories.push(directory);
        const distDirectory = join(directory, "dist");
        const runtimeDirectory = join(directory, "runtime");
        await mkdir(distDirectory);
        await mkdir(runtimeDirectory);
        await writeFile(join(distDirectory, "index.html"), "<p>Fei Editor</p>", "utf8");
        await writeFile(
            join(runtimeDirectory, "sample-browser-project.html"),
            "<p>Fei Runtime</p>",
            "utf8",
        );
        const host = createEditorHost({
            credentials: new EncryptedCredentialStore(
                join(directory, "credentials.json"),
                testProtector,
            ),
            distDirectory,
            runtimeDirectory,
            port: 0,
        });
        const address = await host.listen();
        const baseUrl = `http://${address.host}:${address.port}`;

        try {
            const runtime = await fetch(`${baseUrl}/sample-browser-project.html`);
            expect(runtime.status).toBe(200);
            expect(runtime.headers.get("content-type")).toBe("text/html; charset=utf-8");
            expect(await runtime.text()).toContain("Fei Runtime");

            const unknown = await fetch(`${baseUrl}/sample-browser-project.map`);
            expect(unknown.status).toBe(404);
        } finally {
            await new Promise<void>((resolveClose) => host.server.close(() => resolveClose()));
        }
    });

    it("serves an authenticated local project without browser file-system APIs", async () => {
        const directory = await mkdtemp(join(tmpdir(), "fei-editor-project-host-"));
        temporaryDirectories.push(directory);
        const distDirectory = join(directory, "dist");
        const projectDirectory = join(directory, "project");
        await mkdir(distDirectory);
        await mkdir(join(projectDirectory, "assets"), { recursive: true });
        await writeFile(join(distDirectory, "index.html"), "<p>Fei Editor</p>", "utf8");
        await writeFile(join(projectDirectory, "project.yaml"), "name: Host project\n", "utf8");
        await writeFile(join(projectDirectory, "assets", "main.luau"), "return {}\n", "utf8");
        const host = createEditorHost({
            credentials: new EncryptedCredentialStore(
                join(directory, "credentials.json"),
                testProtector,
            ),
            distDirectory,
            runtimeDirectory: distDirectory,
            projectDirectory,
            port: 0,
        });
        const address = await host.listen();
        const baseUrl = `http://${address.host}:${address.port}`;

        try {
            const bootstrap = await fetch(`${baseUrl}/api/v1/bootstrap`).then((response) => response.json());
            expect(bootstrap.project).toMatchObject({ open: true, name: "project" });
            const headers = { Authorization: `Bearer ${bootstrap.token}` };
            const files = await fetch(`${baseUrl}/api/v1/project/files`, { headers });
            expect(files.status).toBe(200);
            expect(await files.json()).toEqual({
                files: [
                    { path: "project.yaml", kind: "text", readonly: false },
                    { path: "assets/main.luau", kind: "text", readonly: false },
                ],
            });

            const saved = await fetch(`${baseUrl}/api/v1/project/file`, {
                method: "PUT",
                headers: { ...headers, "Content-Type": "application/json" },
                body: JSON.stringify({ path: "assets/main.luau", content: "return 42\n" }),
            });
            expect(saved.status).toBe(200);
            expect(await readFile(join(projectDirectory, "assets", "main.luau"), "utf8")).toBe(
                "return 42\n",
            );

            const unauthenticated = await fetch(`${baseUrl}/api/v1/project/files`);
            expect(unauthenticated.status).toBe(401);
        } finally {
            await new Promise<void>((resolveClose) => host.server.close(() => resolveClose()));
        }
    });
});
