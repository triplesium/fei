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
    unlink,
    writeFile,
} from "node:fs/promises";
import { basename, dirname, resolve, sep } from "node:path";

const textFilePattern =
    /\.(?:c|cc|cpp|cxx|h|hh|hpp|hxx|json|lua|luau|md|slang|txt|wgsl|ya?ml)$/i;

export interface HostProjectFileEntry {
    path: string;
    kind: "text" | "binary" | "directory";
    readonly: boolean;
}

export interface HostProjectSnapshot {
    open: boolean;
    name?: string;
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
        return this.root ? { open: true, name: basename(this.root) } : { open: false };
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
        await mkdir(dirname(destinationFile), { recursive: true });
        await rename(sourceFile, destinationFile);
    }

    async remove(path: string): Promise<void> {
        const file = await this.existingFile(path, true);
        if (!file) throw new Error(`Project file not found: ${path}`);
        await unlink(file);
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
