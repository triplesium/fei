import { mkdir, mkdtemp, readFile, rm, symlink, writeFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { afterEach, describe, expect, it } from "vitest";
import { HostProjectService, ProjectPickerCancelledError } from "../src/workspace/project-service.js";

const temporaryDirectories: string[] = [];

async function createProject(): Promise<string> {
    const directory = await mkdtemp(join(tmpdir(), "entisium-editor-project-"));
    temporaryDirectories.push(directory);
    await mkdir(join(directory, "assets"));
    await writeFile(join(directory, "project.yaml"), "name: Test\n", "utf8");
    await writeFile(join(directory, "assets", "main.luau"), "return {}\n", "utf8");
    return directory;
}

afterEach(async () => {
    await Promise.all(
        temporaryDirectories.splice(0).map((directory) =>
            rm(directory, { recursive: true, force: true }),
        ),
    );
});

describe("HostProjectService", () => {
    it("opens, lists, reads, writes, renames, and removes project files", async () => {
        const directory = await createProject();
        const service = new HostProjectService(directory);

        expect(await service.snapshot()).toMatchObject({ open: true });
        expect(await service.list()).toEqual([
            { path: "project.yaml", kind: "text", readonly: false },
            { path: "assets/main.luau", kind: "text", readonly: false },
        ]);
        expect((await service.read("assets/main.luau"))?.toString("utf8")).toBe("return {}\n");

        const metadataId = "6d174b91-4ab0-4329-bc43-3d3872f8451d";
        await writeFile(
            join(directory, "assets", "main.luau.meta"),
            `id: ${metadataId}\nimporter: native\nsettings:\n  mode: test\n`,
            "utf8",
        );
        expect(await service.list()).not.toContainEqual(
            expect.objectContaining({ path: "assets/main.luau.meta" }),
        );
        expect(await service.inspect("assets/main.luau")).toMatchObject({
            path: "assets/main.luau",
            assetType: "script",
            lineCount: 1,
            metadata: {
                id: metadataId,
                importer: "native",
                settings: { mode: "test" },
                state: "imported",
            },
        });

        await service.createDirectory("assets/scripts");
        expect(await service.list()).toContainEqual({
            path: "assets/scripts",
            kind: "directory",
            readonly: false,
        });
        await service.write("assets/new.luau", "return 42\n");
        await writeFile(
            join(directory, "assets", "new.luau.meta"),
            `id: ${metadataId}\nimporter: native\nsettings: {}\n`,
            "utf8",
        );
        await service.rename("assets/new.luau", "assets/moved.luau");
        expect(await readFile(join(directory, "assets", "moved.luau"), "utf8")).toBe(
            "return 42\n",
        );
        expect(await readFile(join(directory, "assets", "moved.luau.meta"), "utf8"))
            .toContain(metadataId);
        await service.remove("assets/moved.luau");
        expect(await service.exists("assets/moved.luau")).toBe(false);
        await expect(readFile(join(directory, "assets", "moved.luau.meta"))).rejects.toMatchObject({
            code: "ENOENT",
        });

        await service.createDirectory("assets/remove-me");
        await service.write("assets/remove-me/nested.luau", "return 7\n");
        expect(await service.inspect("assets/remove-me")).toMatchObject({
            assetType: "folder",
            fileCount: 1,
            directoryCount: 0,
        });
        await service.remove("assets/remove-me");
        expect(await service.list()).not.toContainEqual(
            expect.objectContaining({ path: "assets/remove-me" }),
        );

        await expect(service.createDirectory("outside")).rejects.toThrow("inside assets");
        service.dispose();
    });

    it("rejects paths outside the project and skips symbolic links", async () => {
        const directory = await createProject();
        const outside = await mkdtemp(join(tmpdir(), "entisium-editor-outside-"));
        temporaryDirectories.push(outside);
        await writeFile(join(outside, "secret.txt"), "secret", "utf8");
        let symbolicLinkCreated = false;
        try {
            await symlink(join(outside, "secret.txt"), join(directory, "assets", "linked.txt"));
            symbolicLinkCreated = true;
        } catch (error) {
            if (!(error && typeof error === "object" && "code" in error && error.code === "EPERM")) {
                throw error;
            }
        }
        const service = new HostProjectService(directory);

        await expect(service.read("../secret.txt")).rejects.toThrow("valid relative");
        if (symbolicLinkCreated) {
            await expect(service.read("assets/linked.txt")).rejects.toThrow("inside the project root");
            expect(await service.list()).not.toContainEqual(
                expect.objectContaining({ path: "assets/linked.txt" }),
            );
        }
        service.dispose();
    });

    it("uses the native picker and reports cancellation", async () => {
        const directory = await createProject();
        const service = new HostProjectService(undefined, async () => directory);
        expect(await service.open()).toMatchObject({ open: true });
        service.dispose();

        const cancelled = new HostProjectService(undefined, async () => undefined);
        await expect(cancelled.open()).rejects.toBeInstanceOf(ProjectPickerCancelledError);
        cancelled.dispose();
    });
});
