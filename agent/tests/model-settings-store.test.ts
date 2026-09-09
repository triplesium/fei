import { mkdtemp, readFile, rm, writeFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { afterEach, describe, expect, it } from "vitest";
import { FileEditorModelSettingsStore } from "../src/models/model-settings-store.js";

const temporaryDirectories: string[] = [];

afterEach(async () => {
    await Promise.all(
        temporaryDirectories.splice(0).map((directory) =>
            rm(directory, { recursive: true, force: true }),
        ),
    );
});

describe("FileEditorModelSettingsStore", () => {
    it("falls back from an unavailable preferred directory", async () => {
        const directory = await mkdtemp(join(tmpdir(), "entisium-editor-model-settings-"));
        temporaryDirectories.push(directory);
        const blockedDirectory = join(directory, "blocked");
        const fallbackPath = join(directory, "fallback", "model-settings.json");
        await writeFile(blockedDirectory, "not a directory", "utf8");
        const store = new FileEditorModelSettingsStore([
            join(blockedDirectory, "model-settings.json"),
            fallbackPath,
        ]);

        await store.write({
            version: 2,
            active: { providerId: "deepseek", modelId: "deepseek-v4-pro" },
            providers: [{
                id: "deepseek",
                name: "DeepSeek",
                baseUrl: "https://api.deepseek.com",
                api: "responses",
                models: [{
                    id: "deepseek-v4-pro",
                    name: "DeepSeek V4 Pro",
                    reasoning: true,
                    contextWindow: 1_000_000,
                    maxTokens: 384_000,
                }],
            }],
        });

        expect(JSON.parse(await readFile(fallbackPath, "utf8"))).toMatchObject({
            active: { providerId: "deepseek", modelId: "deepseek-v4-pro" },
        });
    });

    it("migrates the legacy single custom provider to the unified registry", async () => {
        const directory = await mkdtemp(join(tmpdir(), "entisium-editor-model-settings-"));
        temporaryDirectories.push(directory);
        const path = join(directory, "model-settings.json");
        await writeFile(path, JSON.stringify({
            version: 1,
            active: { providerId: "custom-openai", modelId: "legacy-model" },
            custom: {
                providerName: "Legacy API",
                baseUrl: "https://legacy.example/v1",
                api: "responses",
                modelId: "legacy-model",
                modelName: "Legacy Model",
                reasoning: false,
                contextWindow: 128_000,
                maxTokens: 8_192,
            },
        }), "utf8");

        const settings = await new FileEditorModelSettingsStore(path).read();

        expect(settings).toMatchObject({
            version: 2,
            active: { providerId: "custom-openai", modelId: "legacy-model" },
            providers: [
                { id: "deepseek" },
                { id: "custom-openai", models: [{ id: "legacy-model" }] },
            ],
        });
    });
});
