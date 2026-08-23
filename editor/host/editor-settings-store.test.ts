import { mkdtemp, readFile, rm, writeFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { afterEach, describe, expect, it } from "vitest";
import { FileEditorSettingsStore } from "./editor-settings-store.js";

const temporaryDirectories: string[] = [];

afterEach(async () => {
    await Promise.all(temporaryDirectories.splice(0).map((path) => rm(path, { recursive: true, force: true })));
});

describe("FileEditorSettingsStore", () => {
    it("persists appearance preferences", async () => {
        const directory = await mkdtemp(join(tmpdir(), "entisium-editor-settings-"));
        temporaryDirectories.push(directory);
        const path = join(directory, "settings.json");
        const store = new FileEditorSettingsStore(path);

        await store.write({ version: 1, appearance: { agentDensity: "comfortable" } });

        expect(await store.read()).toEqual({
            version: 1,
            appearance: { agentDensity: "comfortable" },
        });
        expect(JSON.parse(await readFile(path, "utf8"))).toMatchObject({
            appearance: { agentDensity: "comfortable" },
        });
    });

    it("uses a fallback path when the preferred directory is unavailable", async () => {
        const directory = await mkdtemp(join(tmpdir(), "entisium-editor-settings-"));
        temporaryDirectories.push(directory);
        const blocked = join(directory, "blocked");
        await writeFile(blocked, "not a directory", "utf8");
        const fallback = join(directory, "fallback", "settings.json");
        const store = new FileEditorSettingsStore([join(blocked, "settings.json"), fallback]);

        await store.write({ version: 1, appearance: { agentDensity: "compact" } });

        expect(JSON.parse(await readFile(fallback, "utf8"))).toMatchObject({
            appearance: { agentDensity: "compact" },
        });
    });
});
