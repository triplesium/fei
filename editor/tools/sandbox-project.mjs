import { access, cp, mkdir } from "node:fs/promises";
import { dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const toolsDirectory = dirname(fileURLToPath(import.meta.url));
const editorDirectory = resolve(toolsDirectory, "..");
const repositoryDirectory = resolve(editorDirectory, "..");
const sourceDirectory = resolve(
    repositoryDirectory,
    "samples",
    "browser_project",
    "project",
);
const sandboxDirectory = resolve(
    repositoryDirectory,
    ".sandbox",
    "editor-project",
);
const sandboxManifest = resolve(sandboxDirectory, "project.yaml");

async function prepareSandboxProject() {
    try {
        await access(sandboxManifest);
        console.log(`Reusing Editor sandbox project: ${sandboxDirectory}`);
        return;
    } catch {
        await mkdir(dirname(sandboxDirectory), { recursive: true });
        await cp(sourceDirectory, sandboxDirectory, {
            recursive: true,
            force: false,
            errorOnExist: true,
        });
        console.log(`Created Editor sandbox project: ${sandboxDirectory}`);
    }
}

await prepareSandboxProject();

if (!process.argv.includes("--prepare-only")) {
    process.env.ETS_EDITOR_PROJECT_DIR = sandboxDirectory;
    await import("../dist/server/main.js");
}
