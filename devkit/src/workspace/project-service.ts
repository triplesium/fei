import { randomBytes } from "node:crypto";
import { watch, type FSWatcher } from "node:fs";
import {
    lstat,
    mkdir,
    readFile,
    readdir,
    realpath,
    rename,
    rm,
    stat,
    writeFile,
} from "node:fs/promises";
import { basename, dirname, resolve, sep } from "node:path";
import { pathToFileURL } from "node:url";
import { parse } from "yaml";

const textFilePattern =
    /\.(?:c|cc|cpp|cxx|h|hh|hpp|hxx|json|lua|luau|md|slang|txt|wgsl|ya?ml)$/i;

const scriptExtensions = new Set([".lua", ".luau"]);
const imageExtensions = new Set([
    ".bmp",
    ".gif",
    ".hdr",
    ".jpeg",
    ".jpg",
    ".png",
    ".svg",
    ".tga",
    ".webp",
]);
const modelExtensions = new Set([".glb", ".gltf", ".obj"]);

function extensionFor(path: string): string {
    const name = basename(path);
    const index = name.lastIndexOf(".");
    return index > 0 ? name.slice(index).toLowerCase() : "";
}

function assetTypeFor(
    extension: string,
    text: boolean,
): HostProjectAssetInspection["assetType"] {
    if (scriptExtensions.has(extension)) return "script";
    if (imageExtensions.has(extension)) return "image";
    if (modelExtensions.has(extension)) return "model";
    return text ? "text" : "binary";
}

function mimeTypeFor(extension: string): string | undefined {
    return {
        ".bmp": "image/bmp",
        ".gif": "image/gif",
        ".jpeg": "image/jpeg",
        ".jpg": "image/jpeg",
        ".png": "image/png",
        ".svg": "image/svg+xml",
        ".webp": "image/webp",
    }[extension];
}

function countLines(source: string): number {
    if (!source) return 0;
    return source.split(/\r?\n/).length - (source.endsWith("\n") ? 1 : 0);
}

function modelDocumentCounts(
    inspection: HostProjectAssetInspection,
    value: unknown,
): void {
    if (!value || typeof value !== "object" || Array.isArray(value)) return;
    const document = value as Record<string, unknown>;
    inspection.nodeCount = Array.isArray(document.nodes) ? document.nodes.length : 0;
    inspection.meshCount = Array.isArray(document.meshes) ? document.meshes.length : 0;
    inspection.materialCount = Array.isArray(document.materials) ? document.materials.length : 0;
}

function inspectModel(
    inspection: HostProjectAssetInspection,
    extension: string,
    source: Buffer,
): void {
    try {
        if (extension === ".obj") {
            const lines = source.toString("utf8").split(/\r?\n/);
            inspection.vertexCount = lines.filter((line) => /^\s*v\s+/.test(line)).length;
            inspection.faceCount = lines.filter((line) => /^\s*f\s+/.test(line)).length;
            return;
        }
        if (extension === ".gltf") {
            modelDocumentCounts(inspection, JSON.parse(source.toString("utf8")));
            return;
        }
        if (
            extension === ".glb" &&
            source.length >= 20 &&
            source.readUInt32LE(0) === 0x46546c67 &&
            source.readUInt32LE(16) === 0x4e4f534a
        ) {
            const jsonLength = source.readUInt32LE(12);
            if (20 + jsonLength <= source.length) {
                modelDocumentCounts(
                    inspection,
                    JSON.parse(source.subarray(20, 20 + jsonLength).toString("utf8")),
                );
            }
        }
    } catch {
        // Invalid model details do not prevent inspecting the source file itself.
    }
}

function parseMetadata(source: string): {
    id: string;
    importer: string;
    settings: Record<string, string>;
} {
    const value = parse(source);
    if (!value || typeof value !== "object" || Array.isArray(value)) {
        throw new Error("Asset metadata must be a mapping.");
    }
    const document = value as Record<string, unknown>;
    if (typeof document.id !== "string" || typeof document.importer !== "string") {
        throw new Error("Asset metadata requires string id and importer fields.");
    }
    const settings: Record<string, string> = {};
    if (document.settings !== undefined) {
        if (
            !document.settings ||
            typeof document.settings !== "object" ||
            Array.isArray(document.settings)
        ) {
            throw new Error("Asset metadata settings must be a mapping.");
        }
        for (const [name, setting] of Object.entries(document.settings)) {
            if (typeof setting !== "string" && typeof setting !== "number" && typeof setting !== "boolean") {
                throw new Error("Asset metadata settings must contain scalar values.");
            }
            settings[name] = String(setting);
        }
    }
    return { id: document.id, importer: document.importer, settings };
}

async function exists(path: string): Promise<boolean> {
    try {
        await stat(path);
        return true;
    } catch (error) {
        if (isMissing(error)) return false;
        throw error;
    }
}

function errorMessage(error: unknown): string {
    return error instanceof Error ? error.message : String(error);
}

export interface HostProjectFileEntry {
    path: string;
    kind: "text" | "binary" | "directory";
    readonly: boolean;
}

export interface HostProjectAssetInspection {
    path: string;
    kind: HostProjectFileEntry["kind"];
    readonly: boolean;
    assetType: "folder" | "script" | "image" | "model" | "text" | "binary";
    extension: string;
    size?: number;
    modifiedAt: string;
    mimeType?: string;
    lineCount?: number;
    fileCount?: number;
    directoryCount?: number;
    vertexCount?: number;
    faceCount?: number;
    nodeCount?: number;
    meshCount?: number;
    materialCount?: number;
    metadata?: {
        id: string;
        importer: string;
        settings: Record<string, string>;
        state: "unimported" | "imported";
    };
    metadataError?: string;
}

export interface HostProjectSnapshot {
    open: boolean;
    name?: string;
    rootUri?: string;
}

export interface HostProjectEvent {
    type: "changed";
    path?: string;
}

export type ProjectDirectoryPicker = () => Promise<string | undefined>;

export class ProjectPickerCancelledError extends Error {
    constructor() {
        super("Project directory selection was cancelled.");
        this.name = "ProjectPickerCancelledError";
    }
}

export class HostProjectService {
    private root: string | undefined;
    private initialDirectory: string | undefined;
    private initialAttempted = false;
    private watcher: FSWatcher | undefined;
    private readonly listeners = new Set<(event: HostProjectEvent) => void>();

    constructor(
        initialDirectory?: string,
        private readonly pickDirectory?: ProjectDirectoryPicker,
    ) {
        this.initialDirectory = initialDirectory?.trim() || undefined;
    }

    async snapshot(): Promise<HostProjectSnapshot> {
        await this.ensureInitialProject();
        return this.root
            ? {
                  open: true,
                  name: basename(this.root),
                  rootUri: pathToFileURL(`${this.root}${sep}`).href,
              }
            : { open: false };
    }

    workspaceRoot(): Promise<string> {
        return this.requireRoot();
    }

    async open(directory?: string): Promise<HostProjectSnapshot> {
        const selected = directory?.trim() || (await this.pickDirectory?.());
        if (!selected) throw new ProjectPickerCancelledError();
        await this.attach(selected);
        return this.snapshot();
    }

    async list(): Promise<HostProjectFileEntry[]> {
        const root = await this.requireRoot();
        const files: HostProjectFileEntry[] = [
            { path: "project.yaml", kind: "text", readonly: false },
        ];
        const assets = resolve(root, "assets");
        try {
            if (!(await lstat(assets)).isDirectory()) return files;
        } catch (error) {
            if (isMissing(error)) return files;
            throw error;
        }
        const visit = async (directory: string, prefix: string): Promise<void> => {
            for (const entry of await readdir(directory, { withFileTypes: true })) {
                if (entry.isSymbolicLink()) continue;
                const path = `${prefix}/${entry.name}`;
                const absolute = resolve(directory, entry.name);
                if (entry.isDirectory()) {
                    files.push({ path, kind: "directory", readonly: false });
                    await visit(absolute, path);
                } else if (entry.isFile()) {
                    if (entry.name.endsWith(".meta")) continue;
                    const text = textFilePattern.test(path);
                    files.push({ path, kind: text ? "text" : "binary", readonly: !text });
                }
            }
        };
        await visit(assets, "assets");
        return files.sort((left, right) => {
            if (left.path === "project.yaml") return -1;
            if (right.path === "project.yaml") return 1;
            return left.path.localeCompare(right.path);
        });
    }

    async inspect(path: string): Promise<HostProjectAssetInspection> {
        const { root, file } = await this.candidate(path);
        const resolved = await realpath(file);
        if (!resolved.startsWith(`${root}${sep}`)) {
            throw new Error("Project path escapes the project root through a symbolic link.");
        }
        const entry = await stat(resolved);
        const extension = extensionFor(path);
        if (entry.isDirectory()) {
            const children = (await readdir(resolved, { withFileTypes: true })).filter(
                (child) => !child.isSymbolicLink() && !child.name.endsWith(".meta"),
            );
            return {
                path,
                kind: "directory",
                readonly: false,
                assetType: "folder",
                extension: "",
                modifiedAt: entry.mtime.toISOString(),
                fileCount: children.filter((child) => child.isFile()).length,
                directoryCount: children.filter((child) => child.isDirectory()).length,
            };
        }
        if (!entry.isFile()) {
            throw new Error("Project path is not a regular file or directory inside the project root.");
        }

        const text = textFilePattern.test(path);
        const assetType = assetTypeFor(extension, text);
        const inspection: HostProjectAssetInspection = {
            path,
            kind: text ? "text" : "binary",
            readonly: !text,
            assetType,
            extension,
            size: entry.size,
            modifiedAt: entry.mtime.toISOString(),
            ...(mimeTypeFor(extension) ? { mimeType: mimeTypeFor(extension) } : {}),
        };
        const source = assetType === "script" || assetType === "text" || assetType === "model"
            ? await readFile(resolved)
            : undefined;
        if (source && (assetType === "script" || assetType === "text")) {
            inspection.lineCount = countLines(source.toString("utf8"));
        }
        if (source && assetType === "model") inspectModel(inspection, extension, source);

        const metadataFile = `${resolved}.meta`;
        try {
            const metadataSource = await readFile(metadataFile, "utf8");
            const metadata = parseMetadata(metadataSource);
            const importRecord = resolve(root, ".entisium", "imported", metadata.id, "import.yaml");
            inspection.metadata = {
                ...metadata,
                state:
                    metadata.importer === "native" || (await exists(importRecord))
                        ? "imported"
                        : "unimported",
            };
        } catch (error) {
            if (!isMissing(error)) inspection.metadataError = errorMessage(error);
        }
        return inspection;
    }

    async read(path: string): Promise<Buffer | undefined> {
        const file = await this.existingFile(path, false);
        if (!file) return undefined;
        return readFile(file);
    }

    async write(path: string, content: Buffer | string): Promise<void> {
        const file = await this.writableFile(path);
        await mkdir(dirname(file), { recursive: true });
        const temporary = `${file}.entisium-${randomBytes(8).toString("hex")}.tmp`;
        try {
            await writeFile(temporary, content, { flag: "wx" });
            await rename(temporary, file);
        } finally {
            await rm(temporary, { force: true });
        }
    }

    async createDirectory(path: string): Promise<void> {
        if (!path.startsWith("assets/")) {
            throw new Error("Project directories must be inside assets/.");
        }
        await mkdir(await this.writableFile(path));
    }

    async exists(path: string): Promise<boolean> {
        return Boolean(await this.existingFile(path, false));
    }

    async rename(source: string, destination: string): Promise<void> {
        const sourceFile = await this.existingFile(source, true);
        if (!sourceFile) throw new Error(`Project file not found: ${source}`);
        if (await this.exists(destination)) {
            throw new Error(`Project file already exists: ${destination}`);
        }
        const destinationFile = await this.writableFile(destination);
        const sourceMetadata = `${sourceFile}.meta`;
        const destinationMetadata = `${destinationFile}.meta`;
        const hasMetadata = await exists(sourceMetadata);
        if (hasMetadata && (await exists(destinationMetadata))) {
            throw new Error(`Project file already exists: ${destination}.meta`);
        }
        await mkdir(dirname(destinationFile), { recursive: true });
        await rename(sourceFile, destinationFile);
        if (hasMetadata) {
            try {
                await rename(sourceMetadata, destinationMetadata);
            } catch (error) {
                await rename(destinationFile, sourceFile);
                throw error;
            }
        }
    }

    async remove(path: string): Promise<void> {
        const { root, file } = await this.candidate(path);
        const resolved = await realpath(file);
        if (!resolved.startsWith(`${root}${sep}`)) {
            throw new Error("Project path escapes the project root through a symbolic link.");
        }
        const entry = await stat(resolved);
        if (!entry.isFile() && !entry.isDirectory()) {
            throw new Error("Project path is not a regular file or directory inside the project root.");
        }
        await rm(resolved, { recursive: entry.isDirectory() });
        if (entry.isFile()) await rm(`${resolved}.meta`, { force: true });
    }

    subscribe(listener: (event: HostProjectEvent) => void): () => void {
        this.listeners.add(listener);
        return () => this.listeners.delete(listener);
    }

    dispose(): void {
        this.watcher?.close();
        this.watcher = undefined;
        this.listeners.clear();
    }

    validatePath(path: string): string {
        if (
            !path ||
            path.startsWith("/") ||
            path.endsWith("/") ||
            path.includes("\\") ||
            path.split("/").some((part) => !part || part === "." || part === "..")
        ) {
            throw new Error("Path must be a valid relative project path.");
        }
        if (path !== "project.yaml" && !path.startsWith("assets/")) {
            throw new Error("Project files must be project.yaml or inside assets/.");
        }
        return path;
    }

    private async ensureInitialProject(): Promise<void> {
        if (this.initialAttempted) return;
        this.initialAttempted = true;
        const directory = this.initialDirectory;
        this.initialDirectory = undefined;
        if (directory) await this.attach(directory);
    }

    private async attach(directory: string): Promise<void> {
        const root = await realpath(resolve(directory));
        if (!(await stat(root)).isDirectory()) throw new Error("Project path is not a directory.");
        const projectFile = resolve(root, "project.yaml");
        try {
            if (!(await stat(projectFile)).isFile()) throw new Error();
        } catch {
            throw new Error("Select a project folder containing project.yaml.");
        }
        this.root = root;
        this.startWatcher(root);
    }

    private async requireRoot(): Promise<string> {
        await this.ensureInitialProject();
        if (!this.root) throw new Error("Open a local project folder first.");
        return this.root;
    }

    private async candidate(path: string): Promise<{ root: string; file: string }> {
        const root = await this.requireRoot();
        const file = resolve(root, this.validatePath(path));
        if (file === root || !file.startsWith(`${root}${sep}`)) {
            throw new Error("Project path escapes the project root.");
        }
        return { root, file };
    }

    private async existingFile(path: string, required: boolean): Promise<string | undefined> {
        const { root, file } = await this.candidate(path);
        try {
            const resolved = await realpath(file);
            if (!resolved.startsWith(`${root}${sep}`) || !(await stat(resolved)).isFile()) {
                throw new Error("Project path is not a regular file inside the project root.");
            }
            return resolved;
        } catch (error) {
            if (!required && isMissing(error)) return undefined;
            throw error;
        }
    }

    private async writableFile(path: string): Promise<string> {
        const { root, file } = await this.candidate(path);
        let parent = dirname(file);
        while (parent !== root) {
            try {
                const resolvedParent = await realpath(parent);
                if (!resolvedParent.startsWith(`${root}${sep}`)) {
                    throw new Error("Project path escapes the project root through a symbolic link.");
                }
                break;
            } catch (error) {
                if (!isMissing(error)) throw error;
                parent = dirname(parent);
            }
        }
        return file;
    }

    private startWatcher(root: string): void {
        this.watcher?.close();
        try {
            this.watcher = watch(root, { recursive: true }, (_event, fileName) => {
                const path = fileName?.toString().replaceAll("\\", "/");
                const allowed = path && (path === "project.yaml" || path.startsWith("assets/"));
                const event: HostProjectEvent = allowed
                    ? { type: "changed", path }
                    : { type: "changed" };
                for (const listener of this.listeners) listener(event);
            });
        } catch {
            this.watcher = undefined;
        }
    }
}

function isMissing(error: unknown): boolean {
    return Boolean(
        error &&
            typeof error === "object" &&
            "code" in error &&
            (error as { code?: unknown }).code === "ENOENT",
    );
}
