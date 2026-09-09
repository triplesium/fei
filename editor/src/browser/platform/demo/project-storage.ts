import type {
    ProjectAssetInspection,
    ProjectFileEntry,
    RememberedProject,
} from "@/types";

const textFilePattern =
    /\.(?:c|cc|cpp|cxx|h|hh|hpp|hxx|json|lua|luau|md|slang|txt|wgsl|ya?ml)$/i;
const scriptExtensions = new Set([".lua", ".luau"]);
const imageMimeTypes: Record<string, string> = {
    ".bmp": "image/bmp",
    ".gif": "image/gif",
    ".jpeg": "image/jpeg",
    ".jpg": "image/jpeg",
    ".png": "image/png",
    ".svg": "image/svg+xml",
    ".webp": "image/webp",
};
const modelExtensions = new Set([".glb", ".gltf", ".obj"]);

function extensionFor(path: string): string {
    const name = path.split("/").at(-1) ?? "";
    const index = name.lastIndexOf(".");
    return index > 0 ? name.slice(index).toLowerCase() : "";
}

function lineCount(source: string): number {
    if (!source) return 0;
    return source.split(/\r?\n/).length - (source.endsWith("\n") ? 1 : 0);
}

export interface ProjectStorageEvent {
    type: "changed";
    path?: string;
}

interface BundledProjectManifest {
    version: 1;
    id: string;
    name: string;
    files: string[];
}

export class ProjectStorage {
    private root: FileSystemDirectoryHandle | null = null;
    private pendingRoot: FileSystemDirectoryHandle | null = null;
    private readonly listeners = new Set<(event: ProjectStorageEvent) => void>();

    get isOpen(): boolean {
        return this.root !== null;
    }

    get rootUri(): string {
        return "file:///workspace/";
    }

    async initialize(): Promise<RememberedProject | null> {
        await this.removeLegacyBundledProjects();
        try {
            const root = await this.loadRememberedRoot();
            if (root?.kind === "directory") {
                const permission = await root.queryPermission({ mode: "readwrite" });
                if (permission === "granted") {
                    await this.attach(root);
                    return { name: root.name, restored: true, source: "local" };
                }
                this.pendingRoot = root;
            }
        } catch (error) {
            console.warn("[entisium editor] could not restore the last project folder", error);
        }
        return this.openBundledProject();
    }

    async open(): Promise<string> {
        if (this.pendingRoot) {
            const root = this.pendingRoot;
            const permission = await root.requestPermission({ mode: "readwrite" });
            if (permission !== "granted") throw new Error(`Access to ${root.name} was not granted.`);
            await this.attach(root);
            return root.name;
        }
        if (typeof window.showDirectoryPicker !== "function") {
            throw new Error("Opening local folders requires a current version of Edge or Chrome.");
        }
        const root = await window.showDirectoryPicker({ mode: "readwrite" });
        await this.attach(root);
        try {
            await this.rememberRoot(root);
        } catch (error) {
            console.warn("[entisium editor] could not remember the project folder", error);
        }
        return root.name;
    }

    assertOpen(): void {
        if (!this.root) throw new Error("Open a local project folder first.");
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

    async read(path: string): Promise<string | null> {
        try {
            return (await (await this.fileHandle(path, false)).getFile()).text();
        } catch (error) {
            if (error instanceof DOMException && error.name === "NotFoundError") return null;
            throw error;
        }
    }

    async bytes(path: string): Promise<Uint8Array<ArrayBuffer>> {
        const file = await (await this.fileHandle(path, false)).getFile();
        return new Uint8Array(await file.arrayBuffer());
    }

    async write(path: string, content: string): Promise<void> {
        const writable = await (await this.fileHandle(path, true)).createWritable();
        await writable.write(content);
        await writable.close();
        this.emit(path);
    }

    async createDirectory(path: string): Promise<void> {
        this.validatePath(path);
        if (!path.startsWith("assets/")) throw new Error("Project directories must be inside assets/.");
        await this.directoryHandle(path, true);
        this.emit(path);
    }

    async list(): Promise<ProjectFileEntry[]> {
        const root = this.requireRoot();
        const files: ProjectFileEntry[] = [{ path: "project.yaml", kind: "text", readonly: false }];
        const visit = async (directory: FileSystemDirectoryHandle, prefix: string): Promise<void> => {
            for await (const [name, handle] of directory.entries()) {
                if (name.endsWith(".meta")) continue;
                const path = `${prefix}/${name}`;
                if (handle.kind === "directory") {
                    files.push({ path, kind: "directory", readonly: false });
                    await visit(handle, path);
                } else {
                    const text = textFilePattern.test(path);
                    files.push({ path, kind: text ? "text" : "binary", readonly: !text });
                }
            }
        };
        try {
            await visit(await root.getDirectoryHandle("assets"), "assets");
        } catch (error) {
            if (!(error instanceof DOMException) || error.name !== "NotFoundError") throw error;
        }
        return files.sort((left, right) => {
            if (left.path === "project.yaml") return -1;
            if (right.path === "project.yaml") return 1;
            return left.path.localeCompare(right.path);
        });
    }

    async inspect(path: string): Promise<ProjectAssetInspection> {
        const handle = await this.entryHandle(path);
        const extension = extensionFor(path);
        if (handle.kind === "directory") {
            let fileCount = 0;
            let directoryCount = 0;
            for await (const [, child] of handle.entries()) {
                if (child.kind === "directory") directoryCount += 1;
                else fileCount += 1;
            }
            return {
                path,
                kind: "directory",
                readonly: false,
                assetType: "folder",
                extension: "",
                modifiedAt: new Date(0).toISOString(),
                fileCount,
                directoryCount,
            };
        }
        const file = await handle.getFile();
        const text = textFilePattern.test(path);
        const assetType = scriptExtensions.has(extension)
            ? "script"
            : extension in imageMimeTypes
              ? "image"
              : modelExtensions.has(extension)
                ? "model"
                : text
                  ? "text"
                  : "binary";
        const inspection: ProjectAssetInspection = {
            path,
            kind: text ? "text" : "binary",
            readonly: !text,
            assetType,
            extension,
            size: file.size,
            modifiedAt: new Date(file.lastModified).toISOString(),
            ...(imageMimeTypes[extension] ? { mimeType: imageMimeTypes[extension] } : {}),
        };
        if (assetType === "script" || assetType === "text") {
            inspection.lineCount = lineCount(await file.text());
        }
        return inspection;
    }

    async exists(path: string): Promise<boolean> {
        try {
            await this.entryHandle(path);
            return true;
        } catch (error) {
            if (error instanceof DOMException && error.name === "NotFoundError") return false;
            throw error;
        }
    }

    async remove(path: string): Promise<void> {
        const { directory, name } = await this.parentHandle(path, false);
        await directory.removeEntry(name, { recursive: true });
        this.emit(path);
    }

    async rename(source: string, destination: string): Promise<void> {
        this.validatePath(source);
        this.validatePath(destination);
        if (source === destination) return;
        if (await this.exists(destination)) throw new Error(`Project file already exists: ${destination}`);
        const sourceHandle = await this.entryHandle(source);
        if (sourceHandle.kind === "file") {
            const target = await this.fileHandle(destination, true);
            const writable = await target.createWritable();
            await writable.write(await sourceHandle.getFile());
            await writable.close();
        } else {
            await this.copyDirectory(sourceHandle, await this.directoryHandle(destination, true));
        }
        await this.remove(source);
        this.emit(destination);
    }

    subscribe(listener: (event: ProjectStorageEvent) => void): () => void {
        this.listeners.add(listener);
        return () => this.listeners.delete(listener);
    }

    dispose(): void {
        this.listeners.clear();
    }

    private requireRoot(): FileSystemDirectoryHandle {
        this.assertOpen();
        return this.root!;
    }

    private async attach(root: FileSystemDirectoryHandle, clearPending = true): Promise<void> {
        try {
            await root.getFileHandle("project.yaml");
        } catch (error) {
            if (error instanceof DOMException && error.name === "NotFoundError") {
                throw new Error("Select a project folder containing project.yaml.");
            }
            throw error;
        }
        this.root = root;
        if (clearPending) this.pendingRoot = null;
    }

    private async openBundledProject(): Promise<RememberedProject | null> {
        const manifestUrl = new URL("./demo-project/manifest.json", window.location.href);
        const response = await fetch(manifestUrl, { cache: "no-store" });
        if (response.status === 404) return null;
        if (!response.ok) throw new Error(`Could not load the demo project (${response.status}).`);
        const value = (await response.json()) as Partial<BundledProjectManifest>;
        if (
            value.version !== 1 ||
            typeof value.id !== "string" ||
            !/^[a-f0-9]{64}$/.test(value.id) ||
            typeof value.name !== "string" ||
            !value.name.trim() ||
            !Array.isArray(value.files) ||
            value.files.some((path) => typeof path !== "string" || this.validatePath(path) !== path)
        ) {
            throw new Error("The bundled demo project manifest is invalid.");
        }
        const manifest = value as BundledProjectManifest;
        const storageRoot = await navigator.storage.getDirectory();
        const projects = await storageRoot.getDirectoryHandle("entisium-editor-demo", {
            create: true,
        });
        const project = await projects.getDirectoryHandle("project", { create: true });
        const marker = await this.readFileFromRoot(project, ".entisium-demo-version");
        if (marker !== manifest.id) {
            await this.clearDirectory(project);
            await this.installBundledProject(project, manifest, manifestUrl);
        }
        await this.attach(project, false);
        return { name: manifest.name, restored: true, source: "bundled" };
    }

    private async removeLegacyBundledProjects(): Promise<void> {
        const storageRoot = await navigator.storage.getDirectory();
        try {
            const projects = await storageRoot.getDirectoryHandle("entisium-editor-demo");
            for await (const [name] of projects.entries()) {
                if (name !== "project") {
                    await projects.removeEntry(name, { recursive: true });
                }
            }
        } catch (error) {
            if (!(error instanceof DOMException) || error.name !== "NotFoundError") throw error;
        }
    }

    private async clearDirectory(directory: FileSystemDirectoryHandle): Promise<void> {
        for await (const [name] of directory.entries()) {
            await directory.removeEntry(name, { recursive: true });
        }
    }

    private async installBundledProject(
        project: FileSystemDirectoryHandle,
        manifest: BundledProjectManifest,
        manifestUrl: URL,
    ): Promise<void> {
        const filesRoot = new URL("files/", manifestUrl);
        for (const path of manifest.files) {
            const encodedPath = path.split("/").map(encodeURIComponent).join("/");
            const fileResponse = await fetch(new URL(encodedPath, filesRoot));
            if (!fileResponse.ok) {
                throw new Error(`Could not load demo project file '${path}'.`);
            }
            await this.writeFileToRoot(project, path, await fileResponse.arrayBuffer());
        }
        await this.writeFileToRoot(project, ".entisium-demo-version", manifest.id);
    }

    private async readFileFromRoot(
        root: FileSystemDirectoryHandle,
        path: string,
    ): Promise<string | null> {
        try {
            return (await (await root.getFileHandle(path)).getFile()).text();
        } catch (error) {
            if (error instanceof DOMException && error.name === "NotFoundError") return null;
            throw error;
        }
    }

    private async writeFileToRoot(
        root: FileSystemDirectoryHandle,
        path: string,
        content: ArrayBuffer | string,
    ): Promise<void> {
        const parts = path.split("/");
        const name = parts.pop()!;
        let directory = root;
        for (const part of parts) {
            directory = await directory.getDirectoryHandle(part, { create: true });
        }
        const writable = await (await directory.getFileHandle(name, { create: true })).createWritable();
        await writable.write(content);
        await writable.close();
    }

    private async parentHandle(path: string, create: boolean): Promise<{ directory: FileSystemDirectoryHandle; name: string }> {
        this.validatePath(path);
        const parts = path.split("/");
        const name = parts.pop()!;
        let directory = this.requireRoot();
        for (const part of parts) directory = await directory.getDirectoryHandle(part, { create });
        return { directory, name };
    }

    private async fileHandle(path: string, create: boolean): Promise<FileSystemFileHandle> {
        const { directory, name } = await this.parentHandle(path, create);
        return directory.getFileHandle(name, { create });
    }

    private async directoryHandle(path: string, create: boolean): Promise<FileSystemDirectoryHandle> {
        this.validatePath(path);
        let directory = this.requireRoot();
        for (const part of path.split("/")) {
            directory = await directory.getDirectoryHandle(part, { create });
        }
        return directory;
    }

    private async entryHandle(path: string): Promise<FileSystemFileHandle | FileSystemDirectoryHandle> {
        const { directory, name } = await this.parentHandle(path, false);
        try {
            return await directory.getFileHandle(name);
        } catch (error) {
            if (!(error instanceof DOMException) || error.name !== "TypeMismatchError") throw error;
            return directory.getDirectoryHandle(name);
        }
    }

    private async copyDirectory(source: FileSystemDirectoryHandle, destination: FileSystemDirectoryHandle): Promise<void> {
        for await (const [name, handle] of source.entries()) {
            if (handle.kind === "directory") {
                await this.copyDirectory(handle, await destination.getDirectoryHandle(name, { create: true }));
            } else {
                const writable = await (await destination.getFileHandle(name, { create: true })).createWritable();
                await writable.write(await handle.getFile());
                await writable.close();
            }
        }
    }

    private emit(path: string): void {
        for (const listener of this.listeners) listener({ type: "changed", path });
    }

    private async database(): Promise<IDBDatabase> {
        return new Promise((resolve, reject) => {
            const request = indexedDB.open("entisium-web-editor", 1);
            request.addEventListener("upgradeneeded", () => {
                if (!request.result.objectStoreNames.contains("settings")) {
                    request.result.createObjectStore("settings");
                }
            });
            request.addEventListener("success", () => resolve(request.result));
            request.addEventListener("error", () => reject(request.error));
        });
    }

    private async storedValue<T>(mode: IDBTransactionMode, operation: (store: IDBObjectStore) => IDBRequest<T>): Promise<T> {
        const database = await this.database();
        try {
            return await new Promise((resolve, reject) => {
                const transaction = database.transaction("settings", mode);
                const request = operation(transaction.objectStore("settings"));
                request.addEventListener("success", () => resolve(request.result));
                request.addEventListener("error", () => reject(request.error));
                transaction.addEventListener("abort", () => reject(transaction.error));
            });
        } finally {
            database.close();
        }
    }

    private loadRememberedRoot(): Promise<FileSystemDirectoryHandle | undefined> {
        return this.storedValue("readonly", (store) => store.get("last-project-root"));
    }

    private rememberRoot(root: FileSystemDirectoryHandle): Promise<IDBValidKey> {
        return this.storedValue("readwrite", (store) => store.put(root, "last-project-root"));
    }
}
