import { mkdir, readFile, rm, mkdtemp, writeFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { afterEach, describe, expect, it } from "vitest";
import {
    EncryptedCredentialStore,
    type SecretProtector,
} from "./credential-store.js";
import { FileEditorModelSettingsStore } from "./model-settings-store.js";
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
        const directory = await mkdtemp(join(tmpdir(), "entisium-editor-host-"));
        temporaryDirectories.push(directory);
        const distDirectory = join(directory, "dist");
        const credentialPath = join(directory, "credentials.json");
        await mkdir(distDirectory);
        await writeFile(join(distDirectory, "index.html"), "<p>Entisium Editor</p>", "utf8");
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
                provider: {
                    id: "deepseek",
                    configured: false,
                    model: { api: "openai-responses" },
                },
            });
            expect(initial).not.toHaveProperty("provider.apiKey");

            const rejected = await fetch(`${baseUrl}/api/v1/model-settings`, {
                method: "PUT",
                headers: {
                    Authorization: `Bearer ${initial.token}`,
                    "Content-Type": "application/json",
                },
                body: JSON.stringify({
                    providerId: initial.provider.id,
                    modelId: initial.provider.model.id,
                    apiKey: "test-deepseek-密钥",
                }),
            });
            expect(rejected.status).toBe(500);
            expect(await rejected.json()).toEqual({
                error: "API key must contain between 8 and 4096 printable ASCII characters without spaces.",
            });

            const saved = await fetch(`${baseUrl}/api/v1/model-settings`, {
                method: "PUT",
                headers: {
                    Authorization: `Bearer ${initial.token}`,
                    "Content-Type": "application/json",
                },
                body: JSON.stringify({
                    providerId: initial.provider.id,
                    modelId: initial.provider.model.id,
                    apiKey: "test-deepseek-secret",
                }),
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
        const directory = await mkdtemp(join(tmpdir(), "entisium-editor-runtime-"));
        temporaryDirectories.push(directory);
        const distDirectory = join(directory, "dist");
        const runtimeDirectory = join(directory, "runtime");
        await mkdir(distDirectory);
        await mkdir(runtimeDirectory);
        await writeFile(join(distDirectory, "index.html"), "<p>Entisium Editor</p>", "utf8");
        await writeFile(
            join(runtimeDirectory, "sample-browser-project.html"),
            "<p>Entisium Runtime</p>",
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
            expect(await runtime.text()).toContain("Entisium Runtime");

            const unknown = await fetch(`${baseUrl}/sample-browser-project.map`);
            expect(unknown.status).toBe(404);
        } finally {
            await new Promise<void>((resolveClose) => host.server.close(() => resolveClose()));
        }
    });

    it("persists a custom Responses API model without exposing its credential", async () => {
        const directory = await mkdtemp(join(tmpdir(), "entisium-editor-custom-model-"));
        temporaryDirectories.push(directory);
        const distDirectory = join(directory, "dist");
        const credentialPath = join(directory, "credentials.json");
        const settingsPath = join(directory, "model-settings.json");
        await mkdir(distDirectory);
        await writeFile(join(distDirectory, "index.html"), "<p>Entisium Editor</p>", "utf8");

        const startHost = () =>
            createEditorHost({
                credentials: new EncryptedCredentialStore(credentialPath, testProtector),
                modelSettingsStore: new FileEditorModelSettingsStore(settingsPath),
                distDirectory,
                runtimeDirectory: distDirectory,
                port: 0,
            });
        let host = startHost();
        let address = await host.listen();
        let baseUrl = `http://${address.host}:${address.port}`;

        try {
            const bootstrap = await fetch(`${baseUrl}/api/v1/bootstrap`).then((response) => response.json());
            const providerConfigured = await fetch(`${baseUrl}/api/v1/model-settings/provider`, {
                method: "PUT",
                headers: {
                    Authorization: `Bearer ${bootstrap.token}`,
                    "Content-Type": "application/json",
                },
                body: JSON.stringify({
                    apiKey: "custom-responses-secret",
                    provider: {
                        id: "custom-openai",
                        name: "Acme AI",
                        baseUrl: "https://models.example.test/v1/",
                        api: "responses",
                    },
                }),
            });
            expect(providerConfigured.status).toBe(200);
            expect(await providerConfigured.json()).toMatchObject({
                providers: [{ id: "deepseek" }, { id: "custom-openai", models: [] }],
            });

            const modelConfigured = await fetch(`${baseUrl}/api/v1/model-settings/model`, {
                method: "PUT",
                headers: {
                    Authorization: `Bearer ${bootstrap.token}`,
                    "Content-Type": "application/json",
                },
                body: JSON.stringify({
                    providerId: "custom-openai",
                    model: {
                            id: "acme-agent-1",
                            name: "Acme Agent 1",
                            reasoning: true,
                            contextWindow: 128_000,
                            maxTokens: 16_384,
                    },
                }),
            });
            expect(modelConfigured.status).toBe(200);

            const configured = await fetch(`${baseUrl}/api/v1/model-settings`, {
                method: "PUT",
                headers: {
                    Authorization: `Bearer ${bootstrap.token}`,
                    "Content-Type": "application/json",
                },
                body: JSON.stringify({
                    providerId: "custom-openai",
                    modelId: "acme-agent-1",
                }),
            });
            expect(configured.status).toBe(200);
            expect(await configured.json()).toMatchObject({
                active: { providerId: "custom-openai", modelId: "acme-agent-1" },
                providers: [
                    { id: "deepseek" },
                    {
                        id: "custom-openai",
                        name: "Acme AI",
                        configured: true,
                        models: [{ id: "acme-agent-1", api: "openai-responses" }],
                    },
                ],
            });
            expect(await readFile(settingsPath, "utf8")).not.toContain("custom-responses-secret");
            expect(await readFile(credentialPath, "utf8")).not.toContain("custom-responses-secret");
        } finally {
            await new Promise<void>((resolveClose) => host.server.close(() => resolveClose()));
        }

        host = startHost();
        address = await host.listen();
        baseUrl = `http://${address.host}:${address.port}`;
        try {
            const restored = await fetch(`${baseUrl}/api/v1/bootstrap`).then((response) => response.json());
            expect(restored.provider).toMatchObject({
                id: "custom-openai",
                name: "Acme AI",
                configured: true,
                model: { id: "acme-agent-1", api: "openai-responses" },
            });
            expect(JSON.stringify(restored)).not.toContain("custom-responses-secret");
        } finally {
            await new Promise<void>((resolveClose) => host.server.close(() => resolveClose()));
        }
    });

    it("serves an authenticated local project without browser file-system APIs", async () => {
        const directory = await mkdtemp(join(tmpdir(), "entisium-editor-project-host-"));
        temporaryDirectories.push(directory);
        const distDirectory = join(directory, "dist");
        const projectDirectory = join(directory, "project");
        await mkdir(distDirectory);
        await mkdir(join(projectDirectory, "assets"), { recursive: true });
        await writeFile(join(distDirectory, "index.html"), "<p>Entisium Editor</p>", "utf8");
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

            const createdDirectory = await fetch(`${baseUrl}/api/v1/project/directory`, {
                method: "POST",
                headers: { ...headers, "Content-Type": "application/json" },
                body: JSON.stringify({ path: "assets/scripts" }),
            });
            expect(createdDirectory.status).toBe(200);
            expect(
                await fetch(`${baseUrl}/api/v1/project/files`, { headers }).then((response) =>
                    response.json(),
                ),
            ).toEqual({
                files: [
                    { path: "project.yaml", kind: "text", readonly: false },
                    { path: "assets/main.luau", kind: "text", readonly: false },
                    { path: "assets/scripts", kind: "directory", readonly: false },
                ],
            });

            const unauthenticated = await fetch(`${baseUrl}/api/v1/project/files`);
            expect(unauthenticated.status).toBe(401);
        } finally {
            await new Promise<void>((resolveClose) => host.server.close(() => resolveClose()));
        }
    });

    it("opens the Host project when model credentials cannot be decrypted", async () => {
        const directory = await mkdtemp(join(tmpdir(), "entisium-editor-project-credential-failure-"));
        temporaryDirectories.push(directory);
        const distDirectory = join(directory, "dist");
        const projectDirectory = join(directory, "project");
        const credentialPath = join(directory, "credentials.json");
        await mkdir(distDirectory);
        await mkdir(projectDirectory);
        await writeFile(join(distDirectory, "index.html"), "<p>Entisium Editor</p>", "utf8");
        await writeFile(join(projectDirectory, "project.yaml"), "name: Host project\n", "utf8");
        await writeFile(
            credentialPath,
            JSON.stringify({ version: 1, protected: "unreadable" }),
            "utf8",
        );
        const unavailableProtector: SecretProtector = {
            protect: async (plaintext) => plaintext,
            unprotect: async () => {
                throw new Error("Credential belongs to another Windows user.");
            },
        };
        const host = createEditorHost({
            credentials: new EncryptedCredentialStore(credentialPath, unavailableProtector),
            distDirectory,
            runtimeDirectory: distDirectory,
            projectDirectory,
            port: 0,
        });
        const address = await host.listen();
        const baseUrl = `http://${address.host}:${address.port}`;

        try {
            const bootstrapResponse = await fetch(`${baseUrl}/api/v1/bootstrap`);
            expect(bootstrapResponse.status).toBe(200);
            expect(await bootstrapResponse.json()).toMatchObject({
                project: { open: true, name: "project" },
                provider: { id: "deepseek", configured: false },
            });
        } finally {
            await new Promise<void>((resolveClose) => host.server.close(() => resolveClose()));
        }
    });
});
