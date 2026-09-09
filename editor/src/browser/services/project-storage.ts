import type {
    ProjectAssetInspection,
    ProjectFileEntry,
    RememberedProject,
} from "../types";
import { EditorHostRequestError, editorHost } from "./editor-host-client";

interface ProjectSnapshot {
    open: boolean;
    name?: string;
    rootUri?: string;
}

interface ProjectFilesResponse {
    files: ProjectFileEntry[];
}

export interface ProjectStorageEvent {
    type: "changed";
    path?: string;
}

export class ProjectStorage {
    private openProject = false;
    private projectRootUri = "";
    private stopEvents: (() => void) | undefined;
    private readonly listeners = new Set<(event: ProjectStorageEvent) => void>();

    get isOpen(): boolean {
        return this.openProject;
    }

    get rootUri(): string {
        return this.projectRootUri;
    }

    async initialize(): Promise<RememberedProject | null> {
        const bootstrap = await editorHost.bootstrap();
        this.openProject = bootstrap.project.open;
        this.projectRootUri = bootstrap.project.rootUri ?? "";
        if (!bootstrap.project.open || !bootstrap.project.name || !this.projectRootUri) return null;
        this.startEvents();
        return { name: bootstrap.project.name, restored: true };
    }

    async open(): Promise<string> {
        try {
            const project = await editorHost.json<ProjectSnapshot>("/api/v1/project/open", {
                method: "POST",
            });
            if (!project.open || !project.name || !project.rootUri) {
                throw new Error("Editor Host did not open a project folder.");
            }
            this.openProject = true;
            this.projectRootUri = project.rootUri;
            this.startEvents();
            return project.name;
        } catch (error) {
            if (error instanceof EditorHostRequestError && error.code === "cancelled") {
                throw new DOMException("Project folder selection was cancelled.", "AbortError");
            }
            throw error;
        }
    }

    assertOpen(): void {
        if (!this.openProject) throw new Error("Open a local project folder first.");
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
            const response = await this.fileResponse(path);
            return new TextDecoder().decode(await response.arrayBuffer());
        } catch (error) {
            if (error instanceof EditorHostRequestError && error.status === 404) return null;
            throw error;
        }
    }

    async bytes(path: string): Promise<Uint8Array<ArrayBuffer>> {
        const response = await this.fileResponse(path);
        return new Uint8Array(await response.arrayBuffer());
    }

    async write(path: string, content: string): Promise<void> {
        this.assertOpen();
        await editorHost.request("/api/v1/project/file", {
            method: "PUT",
            headers: { "Content-Type": "application/json" },
            body: JSON.stringify({ path: this.validatePath(path), content }),
        });
    }

    async createDirectory(path: string): Promise<void> {
        this.assertOpen();
        await editorHost.request("/api/v1/project/directory", {
            method: "POST",
            headers: { "Content-Type": "application/json" },
            body: JSON.stringify({ path: this.validatePath(path) }),
        });
    }

    async list(): Promise<ProjectFileEntry[]> {
        this.assertOpen();
        const result = await editorHost.json<ProjectFilesResponse>("/api/v1/project/files");
        return result.files;
    }

    async inspect(path: string): Promise<ProjectAssetInspection> {
        this.assertOpen();
        return editorHost.json<ProjectAssetInspection>(
            `/api/v1/project/inspect?path=${encodeURIComponent(this.validatePath(path))}`,
        );
    }

    async exists(path: string): Promise<boolean> {
        try {
            await this.fileResponse(path);
            return true;
        } catch (error) {
            if (error instanceof EditorHostRequestError && error.status === 404) return false;
            throw error;
        }
    }

    async remove(path: string): Promise<void> {
        this.assertOpen();
        await editorHost.request(
            `/api/v1/project/file?path=${encodeURIComponent(this.validatePath(path))}`,
            { method: "DELETE" },
        );
    }

    async rename(source: string, destination: string): Promise<void> {
        this.assertOpen();
        await editorHost.request("/api/v1/project/rename", {
            method: "POST",
            headers: { "Content-Type": "application/json" },
            body: JSON.stringify({
                source: this.validatePath(source),
                destination: this.validatePath(destination),
            }),
        });
    }

    subscribe(listener: (event: ProjectStorageEvent) => void): () => void {
        this.listeners.add(listener);
        return () => this.listeners.delete(listener);
    }

    dispose(): void {
        this.stopEvents?.();
        this.stopEvents = undefined;
        this.listeners.clear();
    }

    private fileResponse(path: string): Promise<Response> {
        this.assertOpen();
        return editorHost.request(
            `/api/v1/project/file?path=${encodeURIComponent(this.validatePath(path))}`,
            { cache: "no-store" },
        );
    }

    private startEvents(): void {
        if (this.stopEvents) return;
        this.stopEvents = editorHost.subscribeEvents("/api/v1/project/events", (value) => {
            if (!value || typeof value !== "object" || Array.isArray(value)) return;
            const event = value as { type?: unknown; path?: unknown };
            if (event.type !== "changed") return;
            const projectEvent: ProjectStorageEvent = {
                type: "changed",
                ...(typeof event.path === "string" ? { path: event.path } : {}),
            };
            for (const listener of this.listeners) listener(projectEvent);
        });
    }
}
