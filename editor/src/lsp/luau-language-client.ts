import * as vscode from "vscode";
import { LanguageClientWrapper } from "monaco-languageclient/lcwrapper";
import { MonacoVscodeApiWrapper } from "monaco-languageclient/vscodeApiWrapper";
import { editorHost } from "../services/editor-host-client";

export type LuauLanguageClientStatus =
    | "stopped"
    | "starting"
    | "ready"
    | "unavailable";

const listeners = new Set<(status: LuauLanguageClientStatus) => void>();
let status: LuauLanguageClientStatus = "stopped";
let apiPromise: Promise<void> | undefined;
let client: LanguageClientWrapper | undefined;
let activeRootUri = "";
let generation = 0;

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
        apiPromise = api.start({ caller: "Entisium Editor" });
    }
    return apiPromise;
}

export function prepareLuauLanguageClient(): Promise<void> {
    return initializeVscodeApi();
}

function websocketUrl(token: string): string {
    const url = new URL("/api/v1/lsp/luau", window.location.href);
    url.protocol = url.protocol === "https:" ? "wss:" : "ws:";
    url.searchParams.set("token", token);
    return url.href;
}

export function subscribeLuauLanguageClient(
    listener: (next: LuauLanguageClientStatus) => void,
): () => void {
    listeners.add(listener);
    listener(status);
    return () => listeners.delete(listener);
}

export async function startLuauLanguageClient(rootUri: string): Promise<void> {
    if (!rootUri) {
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
        const bootstrap = await editorHost.bootstrap(true);
        if (requestedGeneration !== generation) return;
        if (client) await client.dispose();

        activeRootUri = rootUri;
        const workspaceUri = vscode.Uri.parse(rootUri);
        client = new LanguageClientWrapper({
            languageId: "luau",
            connection: {
                options: {
                    $type: "WebSocketUrl",
                    url: websocketUrl(bootstrap.token),
                    startOptions: {
                        onCall: () => {
                            if (requestedGeneration === generation) publish("ready");
                        },
                    },
                    stopOptions: {
                        onCall: () => {
                            if (requestedGeneration === generation) publish("unavailable");
                        },
                    },
                },
            },
            clientOptions: {
                documentSelector: [{ language: "luau", scheme: "file" }],
                workspaceFolder: {
                    index: 0,
                    name: bootstrap.project.name ?? "Entisium project",
                    uri: workspaceUri,
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
        console.error("[entisium editor] failed to start Luau language client", error);
        publish("unavailable");
    }
}
