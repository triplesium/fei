import type { ProjectFileEntry, RememberedProject } from "../types";

const textFilePattern =
    /\.(?:c|cc|cpp|cxx|h|hh|hpp|hxx|json|lua|luau|md|slang|txt|wgsl|ya?ml)$/i;

export class ProjectStorage {
    private root: FileSystemDirectoryHandle | null = null;
    private pendingRoot: FileSystemDirectoryHandle | null = null;

    get isOpen(): boolean {
        return this.root !== null;
    }

    get pendingName(): string | null {
        return this.pendingRoot?.name ?? null;
    }

    async initialize(): Promise<RememberedProject | null> {
        if (typeof window.showDirectoryPicker !== "function") {
            throw new Error(
                "The local folder editor requires a current version of Edge or Chrome.",
            );
        }
        try {
            const root = await this.loadRememberedRoot();
            if (!root || root.kind !== "directory") return null;
            const permission = await root.queryPermission({ mode: "readwrite" });
            if (permission === "granted") {
                await this.attach(root);
                return { name: root.name, restored: true };
            }
            this.pendingRoot = root;
            return { name: root.name, permissionRequired: true };
        } catch (error) {
            console.warn("[fei editor] could not restore the last project folder", error);
            return null;
        }
    }

    async open(): Promise<string> {
        if (this.pendingRoot) {
            const root = this.pendingRoot;
            const permission = await root.requestPermission({ mode: "readwrite" });
            if (permission !== "granted") {
                throw new Error(`Access to ${root.name} was not granted.`);
            }
            await this.attach(root);
            return root.name;
        }
        const root = await window.showDirectoryPicker({ mode: "readwrite" });
        await this.attach(root);
        try {
            await this.rememberRoot(root);
        } catch (error) {
            console.warn("[fei editor] could not remember the project folder", error);
        }
        return root.name;
    }

    assertOpen(): void {
        if (!this.root) throw new Error("Open a local project folder first.");
    }

    private requireRoot(): FileSystemDirectoryHandle {
        this.assertOpen();
        return this.root!;
    }

    validatePath(path: string): string {
        if (
            path.length === 0 ||
            path.startsWith("/") ||
            path.endsWith("/") ||
            path.includes("\\") ||
            path.split("/").some((part) => part.length === 0 || part === "." || part === "..")
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
    }

    async list(): Promise<ProjectFileEntry[]> {
        this.assertOpen();
        const files: ProjectFileEntry[] = [
            { path: "project.yaml", kind: "text", readonly: false },
        ];
        const visit = async (directory: FileSystemDirectoryHandle, prefix: string) => {
            for await (const [name, handle] of directory.entries()) {
                const path = `${prefix}/${name}`;
                if (handle.kind === "directory") {
                    await visit(handle, path);
                } else {
                    const text = textFilePattern.test(path);
                    files.push({ path, kind: text ? "text" : "binary", readonly: !text });
                }
            }
        };
        try {
            await visit(await this.requireRoot().getDirectoryHandle("assets"), "assets");
        } catch (error) {
            if (!(error instanceof DOMException) || error.name !== "NotFoundError") throw error;
        }
        return files.sort((left, right) => {
            if (left.path === "project.yaml") return -1;
            if (right.path === "project.yaml") return 1;
            return left.path.localeCompare(right.path);
        });
    }

    async exists(path: string): Promise<boolean> {
        try {
            await this.fileHandle(path, false);
            return true;
        } catch (error) {
            if (error instanceof DOMException && error.name === "NotFoundError") return false;
            throw error;
        }
    }

    async remove(path: string): Promise<void> {
        this.assertOpen();
        this.validatePath(path);
        const parts = path.split("/");
        const fileName = parts.pop()!;
        let directory = this.requireRoot();
        for (const part of parts) directory = await directory.getDirectoryHandle(part);
        await directory.removeEntry(fileName);
    }

    async rename(source: string, destination: string): Promise<void> {
        this.validatePath(source);
        this.validatePath(destination);
        if (source === destination) return;
        if (await this.exists(destination)) {
            throw new Error(`Project file already exists: ${destination}`);
        }
        const content = await this.read(source);
        if (content === null) throw new Error(`Project file not found: ${source}`);
        await this.write(destination, content);
        await this.remove(source);
    }

    private async attach(root: FileSystemDirectoryHandle): Promise<void> {
        try {
            await root.getFileHandle("project.yaml");
        } catch (error) {
            if (error instanceof DOMException && error.name === "NotFoundError") {
                throw new Error("Select a project folder containing project.yaml.");
            }
            throw error;
        }
        this.root = root;
        this.pendingRoot = null;
    }

    private async fileHandle(path: string, create: boolean): Promise<FileSystemFileHandle> {
        this.assertOpen();
        this.validatePath(path);
        const parts = path.split("/");
        const fileName = parts.pop()!;
        let directory = this.requireRoot();
        for (const part of parts) {
            directory = await directory.getDirectoryHandle(part, { create });
        }
        return directory.getFileHandle(fileName, { create });
    }

    private async database(): Promise<IDBDatabase> {
        return new Promise((resolve, reject) => {
            const request = indexedDB.open("fei-web-editor", 1);
            request.addEventListener("upgradeneeded", () => {
                if (!request.result.objectStoreNames.contains("settings")) {
                    request.result.createObjectStore("settings");
                }
            });
            request.addEventListener("success", () => resolve(request.result));
            request.addEventListener("error", () => reject(request.error));
        });
    }

    private async storedValue<T>(
        mode: IDBTransactionMode,
        operation: (store: IDBObjectStore) => IDBRequest<T>,
    ): Promise<T> {
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
