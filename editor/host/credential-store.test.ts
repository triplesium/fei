import { readFile, rm, mkdtemp, writeFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { afterEach, describe, expect, it } from "vitest";
import {
    EncryptedCredentialStore,
    type SecretProtector,
} from "./credential-store.js";

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

describe("EncryptedCredentialStore", () => {
    it("persists credentials through its protector without writing plaintext", async () => {
        const directory = await mkdtemp(join(tmpdir(), "fei-editor-credentials-"));
        temporaryDirectories.push(directory);
        const path = join(directory, "credentials.json");
        const store = new EncryptedCredentialStore(path, testProtector);

        await store.modify("deepseek", async () => ({
            type: "api_key",
            key: "secret-deepseek-key",
        }));

        expect(await readFile(path, "utf8")).not.toContain("secret-deepseek-key");
        const reloaded = new EncryptedCredentialStore(path, testProtector);
        expect(await reloaded.read("deepseek")).toEqual({
            type: "api_key",
            key: "secret-deepseek-key",
        });

        await reloaded.delete("deepseek");
        expect(await reloaded.read("deepseek")).toBeUndefined();
    });

    it("falls back when the preferred credential directory is unavailable", async () => {
        const directory = await mkdtemp(join(tmpdir(), "fei-editor-credential-fallback-"));
        temporaryDirectories.push(directory);
        const blockedDirectory = join(directory, "blocked");
        const fallbackPath = join(directory, "fallback", "credentials.json");
        await writeFile(blockedDirectory, "not a directory", "utf8");
        const store = new EncryptedCredentialStore(
            [join(blockedDirectory, "credentials.json"), fallbackPath],
            testProtector,
        );

        await store.modify("deepseek", async () => ({
            type: "api_key",
            key: "fallback-secret",
        }));

        expect(await readFile(fallbackPath, "utf8")).not.toContain("fallback-secret");
        expect(await store.read("deepseek")).toEqual({
            type: "api_key",
            key: "fallback-secret",
        });
    });
});
