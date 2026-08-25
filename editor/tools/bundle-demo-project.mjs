import { createHash } from "node:crypto";
import { copyFile, mkdir, readFile, readdir, stat, writeFile } from "node:fs/promises";
import { dirname, relative, resolve, sep } from "node:path";

const [sourceArgument, destinationArgument] = process.argv.slice(2);
if (!sourceArgument || !destinationArgument) {
    throw new Error("usage: bundle-demo-project.mjs <project-directory> <output-directory>");
}

const sourceRoot = resolve(sourceArgument);
const destinationRoot = resolve(destinationArgument);
if (!(await stat(sourceRoot)).isDirectory()) {
    throw new Error(`Demo project is not a directory: ${sourceRoot}`);
}
if (!(await stat(resolve(sourceRoot, "project.yaml"))).isFile()) {
    throw new Error(`Demo project has no project.yaml: ${sourceRoot}`);
}

const files = [];
async function visit(directory) {
    const entries = await readdir(directory, { withFileTypes: true });
    entries.sort((left, right) => left.name.localeCompare(right.name));
    for (const entry of entries) {
        const absolute = resolve(directory, entry.name);
        const path = relative(sourceRoot, absolute).split(sep).join("/");
        if (entry.isSymbolicLink()) {
            throw new Error(`Demo projects cannot contain symbolic links: ${path}`);
        }
        if (entry.isDirectory()) {
            await visit(absolute);
        } else if (entry.isFile() && (path === "project.yaml" || path.startsWith("assets/"))) {
            files.push({ absolute, path });
        }
    }
}
await visit(sourceRoot);

const hash = createHash("sha256");
for (const file of files) {
    const content = await readFile(file.absolute);
    hash.update(file.path);
    hash.update("\0");
    hash.update(content);
    const output = resolve(destinationRoot, "files", ...file.path.split("/"));
    if (!output.startsWith(`${destinationRoot}${sep}`)) {
        throw new Error(`Invalid demo project path: ${file.path}`);
    }
    await mkdir(dirname(output), { recursive: true });
    await copyFile(file.absolute, output);
}

const projectSource = await readFile(resolve(sourceRoot, "project.yaml"), "utf8");
const nameMatch = /^name:\s*(?:"([^"]*)"|'([^']*)'|([^\r\n#]+))/m.exec(projectSource);
const name = (nameMatch?.[1] ?? nameMatch?.[2] ?? nameMatch?.[3] ?? "Demo Project").trim();
const manifest = {
    version: 1,
    id: hash.digest("hex"),
    name,
    files: files.map((file) => file.path),
};
await mkdir(destinationRoot, { recursive: true });
await writeFile(
    resolve(destinationRoot, "manifest.json"),
    `${JSON.stringify(manifest, null, 2)}\n`,
);
