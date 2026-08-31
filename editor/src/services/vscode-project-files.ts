import {
    FileSystemProviderError,
    FileSystemProviderErrorCode,
    RegisteredFile,
    RegisteredFileSystemProvider,
    registerFileSystemOverlay,
} from "@codingame/monaco-vscode-files-service-override";
import { URI } from "@codingame/monaco-vscode-api/vscode/vs/base/common/uri";
import { documentUri, planMemoryDirectories } from "../lsp/document-uri";
import type { ProjectFileEntry } from "../types";

export interface VscodeProjectStorage {
    readonly rootUri: string;
    read(path: string): Promise<string | null>;
    write(path: string, content: string): Promise<void>;
}

class ProjectBackedFile extends RegisteredFile {
    constructor(
        uri: URI,
        private readonly storage: VscodeProjectStorage,
        readonly projectPath: string,
    ) {
        super(uri, false);
    }

    async getSize(): Promise<number> {
        return (await this.read()).byteLength;
    }

    async read(): Promise<Uint8Array> {
        const content = await this.storage.read(this.projectPath);
        if (content === null) {
            throw FileSystemProviderError.create(
                `Project file does not exist: ${this.projectPath}`,
                FileSystemProviderErrorCode.FileNotFound,
            );
        }
        return new TextEncoder().encode(content);
    }

    async write(content: Uint8Array): Promise<void> {
        await this.storage.write(this.projectPath, new TextDecoder().decode(content));
        this.notifyChanged();
    }

    notifyChanged(): void {
        this.mtime = Date.now();
        this._onDidChange.fire();
    }
}

class ProjectFileBridge {
    private readonly provider = new RegisteredFileSystemProvider(false);
    private readonly overlay = registerFileSystemOverlay(10, this.provider);
    private readonly files = new Map<
        string,
        {
            file: ProjectBackedFile;
            registration: { dispose(): void };
        }
    >();
    private readonly pathsByResource = new Map<string, string>();
    private readonly driveRoots = new Set<string>();

    constructor(
        readonly storage: VscodeProjectStorage,
        readonly rootUri: string,
    ) {}

    synchronize(entries: readonly ProjectFileEntry[]): void {
        const available = new Set(
            entries.filter((entry) => entry.kind === "text").map((entry) => entry.path),
        );

        for (const [path, registered] of this.files) {
            if (available.has(path)) continue;
            this.pathsByResource.delete(registered.file.uri.toString());
            registered.registration.dispose();
            this.files.delete(path);
        }

        for (const path of available) {
            if (this.files.has(path)) continue;
            // VS Code canonicalizes Windows drive letters while resolving editor inputs.
            // Register the same round-tripped URI so the case-sensitive overlay provider
            // and the language client refer to one resource identity.
            const uri = URI.parse(URI.parse(documentUri(this.rootUri, path)).toString());
            const driveRoot = planMemoryDirectories(uri.path).driveRoot;
            if (driveRoot && !this.driveRoots.has(driveRoot)) {
                this.provider.mkdirSync(
                    uri.with({ scheme: "entisium-project-root", path: driveRoot }),
                );
                this.driveRoots.add(driveRoot);
            }
            const file = new ProjectBackedFile(uri, this.storage, path);
            let registration: { dispose(): void };
            try {
                registration = this.provider.registerFile(file);
            } catch (error) {
                throw new Error(`Could not register project file ${uri.toString()}`, {
                    cause: error,
                });
            }
            this.files.set(path, { file, registration });
            this.pathsByResource.set(uri.toString(), path);
        }
    }

    resource(path: string): URI | undefined {
        return this.files.get(path)?.file.uri;
    }

    path(resource: URI | undefined): string {
        return resource ? (this.pathsByResource.get(resource.toString()) ?? "") : "";
    }

    notifyChanged(path: string): void {
        this.files.get(path)?.file.notifyChanged();
    }

    dispose(): void {
        for (const registered of this.files.values()) registered.registration.dispose();
        this.files.clear();
        this.pathsByResource.clear();
        this.driveRoots.clear();
        this.overlay.dispose();
        this.provider.dispose();
    }
}

let activeBridge: ProjectFileBridge | undefined;

export function synchronizeVscodeProjectFiles(
    storage: VscodeProjectStorage,
    entries: readonly ProjectFileEntry[],
): void {
    if (
        !activeBridge ||
        activeBridge.storage !== storage ||
        activeBridge.rootUri !== storage.rootUri
    ) {
        activeBridge?.dispose();
        activeBridge = new ProjectFileBridge(storage, storage.rootUri);
    }
    activeBridge.synchronize(entries);
}

export function resetVscodeProjectFiles(): void {
    activeBridge?.dispose();
    activeBridge = undefined;
}

export function notifyVscodeProjectFileChanged(path: string): void {
    activeBridge?.notifyChanged(path);
}

export function vscodeProjectResource(path: string): URI {
    const resource = activeBridge?.resource(path);
    if (!resource) throw new Error(`Project file is not registered with VS Code: ${path}`);
    return resource;
}

export function vscodeProjectPath(resource: URI | undefined): string {
    return activeBridge?.path(resource) ?? "";
}
