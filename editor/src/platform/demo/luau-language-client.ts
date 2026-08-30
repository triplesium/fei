import * as vscode from "vscode";
import { LanguageClientWrapper } from "monaco-languageclient/lcwrapper";
import { MonacoVscodeApiWrapper } from "monaco-languageclient/vscodeApiWrapper";

export type LuauLanguageClientStatus =
    | "stopped"
    | "starting"
    | "ready"
    | "unavailable";

export interface LuauProjectFile {
    path: string;
    content: string;
}

interface WorkerControlMessage {
    type?: unknown;
    id?: unknown;
    message?: unknown;
}

class WasmLspBridge {
    readonly worker: Worker;
    readonly lspPort: MessagePort;

    private nextRequest = 1;
    private readonly pending = new Map<
        number,
        { resolve(): void; reject(error: Error): void }
    >();

    private constructor(worker: Worker, lspPort: MessagePort) {
        this.worker = worker;
        this.lspPort = lspPort;
        worker.addEventListener("message", (event: MessageEvent<WorkerControlMessage>) => {
            const message = event.data;
            if (message?.type === "synced" && typeof message.id === "number") {
                this.pending.get(message.id)?.resolve();
                this.pending.delete(message.id);
            } else if (message?.type === "error") {
                const error = new Error(
                    typeof message.message === "string"
                        ? message.message
                        : "The WebAssembly Luau LSP failed.",
                );
                for (const request of this.pending.values()) request.reject(error);
                this.pending.clear();
            }
        });
    }

    static async create(files: readonly LuauProjectFile[]): Promise<WasmLspBridge> {
        const worker = new Worker(
            new URL("../../lsp/luau-lsp.worker.ts", import.meta.url),
            { type: "module", name: "entisium-luau-lsp" },
        );
        const channel = new MessageChannel();
        const ready = new Promise<void>((resolve, reject) => {
            const onMessage = (event: MessageEvent<WorkerControlMessage>) => {
                if (event.data?.type === "ready") {
                    worker.removeEventListener("message", onMessage);
                    resolve();
                } else if (event.data?.type === "error") {
                    worker.removeEventListener("message", onMessage);
                    reject(
                        new Error(
                            typeof event.data.message === "string"
                                ? event.data.message
                                : "The WebAssembly Luau LSP failed to start.",
                        ),
                    );
                }
            };
            worker.addEventListener("message", onMessage);
            worker.addEventListener("error", (event) => reject(event.error ?? event.message), {
                once: true,
            });
        });
        worker.postMessage(
            {
                type: "initialize",
                moduleUrl: new URL("lsp/entisium-lsp.mjs", document.baseURI).href,
                files,
                port: channel.port1,
            },
            [channel.port1],
        );
        try {
            await ready;
            return new WasmLspBridge(worker, channel.port2);
        } catch (error) {
            channel.port2.close();
            worker.terminate();
            throw error;
        }
    }

    sync(files: readonly LuauProjectFile[]): Promise<void> {
        const id = this.nextRequest++;
        const result = new Promise<void>((resolve, reject) => {
            this.pending.set(id, { resolve, reject });
        });
        this.worker.postMessage({ type: "sync", id, files });
        return result;
    }

    dispose(): void {
        this.lspPort.close();
        this.worker.terminate();
        const error = new Error("The WebAssembly Luau LSP was stopped.");
        for (const request of this.pending.values()) request.reject(error);
        this.pending.clear();
    }
}

const listeners = new Set<(status: LuauLanguageClientStatus) => void>();
let status: LuauLanguageClientStatus = "stopped";
let apiPromise: Promise<void> | undefined;
let client: LanguageClientWrapper | undefined;
let bridge: WasmLspBridge | undefined;
let activeRootUri = "";
let generation = 0;
let projectFiles: readonly LuauProjectFile[] = [];
let synchronizedFiles = new Map<string, string>();
let syncSequence = Promise.resolve();

function publish(next: LuauLanguageClientStatus): void {
    if (status === next) return;
    status = next;
    for (const listener of listeners) listener(next);
}

function initializeVscodeApi(): Promise<void> {
    if (!apiPromise) {
        const api = new MonacoVscodeApiWrapper({
            $type: "classic",
            viewsConfig: { $type: "EditorService" },
            monacoWorkerFactory: () => undefined,
            userConfiguration: {
                json: JSON.stringify({
                    "luau-lsp.platform.type": "standard",
                    "luau-lsp.sourcemap.enabled": false,
                    "luau-lsp.types.roblox": false,
                    "luau-lsp.fflags.override": {
                        LuauExportValueSyntax: "true",
                    },
                }),
            },
            advanced: {
                enableExtHostWorker: false,
                loadExtensionServices: false,
                loadThemes: false,
            },
        });
        apiPromise = api.start({ caller: "Entisium Editor Demo" });
    }
    return apiPromise;
}

export function prepareLuauLanguageClient(): Promise<void> {
    return initializeVscodeApi();
}

export function subscribeLuauLanguageClient(
    listener: (next: LuauLanguageClientStatus) => void,
): () => void {
    listeners.add(listener);
    listener(status);
    return () => listeners.delete(listener);
}

function fileUri(path: string): string {
    return `file:///workspace/${path.split("/").map(encodeURIComponent).join("/")}`;
}

function changedFiles(
    previous: ReadonlyMap<string, string>,
    next: ReadonlyMap<string, string>,
): { uri: string; type: 1 | 2 | 3 }[] {
    const changes: { uri: string; type: 1 | 2 | 3 }[] = [];
    for (const [path, content] of next) {
        const oldContent = previous.get(path);
        if (oldContent === undefined) changes.push({ uri: fileUri(path), type: 1 });
        else if (oldContent !== content) changes.push({ uri: fileUri(path), type: 2 });
    }
    for (const path of previous.keys()) {
        if (!next.has(path)) changes.push({ uri: fileUri(path), type: 3 });
    }
    return changes;
}

export function synchronizeLuauLanguageClientProject(
    files: readonly LuauProjectFile[],
): Promise<void> {
    projectFiles = files.map((file) => ({ ...file }));
    if (!bridge) return Promise.resolve();
    syncSequence = syncSequence.then(async () => {
        const next = new Map(projectFiles.map((file) => [file.path, file.content]));
        const changes = changedFiles(synchronizedFiles, next);
        await bridge!.sync(projectFiles);
        synchronizedFiles = next;
        const languageClient = client?.getLanguageClient();
        if (languageClient && changes.length > 0) {
            await languageClient.sendNotification("workspace/didChangeWatchedFiles", {
                changes,
            });
        }
    });
    return syncSequence;
}

export async function startLuauLanguageClient(rootUri: string): Promise<void> {
    if (!rootUri) {
        publish("unavailable");
        return;
    }
    if (!crossOriginIsolated || typeof SharedArrayBuffer === "undefined") {
        console.error(
            "[entisium editor] WebAssembly Luau LSP requires cross-origin isolation",
        );
        publish("unavailable");
        return;
    }
    if (client?.isStarted() && activeRootUri === rootUri) {
        publish("ready");
        return;
    }

    const requestedGeneration = ++generation;
    publish("starting");
    try {
        await prepareLuauLanguageClient();
        if (client) await client.dispose(true);
        bridge?.dispose();
        client = undefined;
        bridge = await WasmLspBridge.create(projectFiles);
        synchronizedFiles = new Map(projectFiles.map((file) => [file.path, file.content]));
        if (requestedGeneration !== generation) {
            bridge.dispose();
            bridge = undefined;
            return;
        }

        activeRootUri = rootUri;
        client = new LanguageClientWrapper({
            languageId: "luau",
            connection: {
                options: {
                    $type: "WorkerDirect",
                    worker: bridge.worker,
                    messagePort: bridge.lspPort,
                },
            },
            clientOptions: {
                documentSelector: [{ language: "luau", scheme: "file" }],
                workspaceFolder: {
                    index: 0,
                    name: "Entisium project",
                    uri: vscode.Uri.parse(rootUri),
                },
                initializationOptions: {
                    fflags: { LuauExportValueSyntax: "true" },
                },
            },
        });
        await client.start();
        if (requestedGeneration === generation) publish("ready");
    } catch (error) {
        if (requestedGeneration !== generation) return;
        console.error("[entisium editor] failed to start WebAssembly Luau LSP", error);
        bridge?.dispose();
        bridge = undefined;
        client = undefined;
        publish("unavailable");
    }
}
