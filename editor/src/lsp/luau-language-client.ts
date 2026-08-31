import * as vscode from "vscode";
import { LanguageClientWrapper } from "monaco-languageclient/lcwrapper";
import { editorHost } from "../services/editor-host-client";
import { waitForVscodeEditorHost } from "../services/vscode-editor-host";

export type LuauLanguageClientStatus =
    | "stopped"
    | "starting"
    | "ready"
    | "unavailable";

export interface LuauProjectFile {
    path: string;
    content: string;
}

const listeners = new Set<(status: LuauLanguageClientStatus) => void>();
let status: LuauLanguageClientStatus = "stopped";
let client: LanguageClientWrapper | undefined;
let activeRootUri = "";
let generation = 0;

function publish(next: LuauLanguageClientStatus): void {
    if (status === next) return;
    status = next;
    for (const listener of listeners) listener(next);
}

export function prepareLuauLanguageClient(): Promise<void> {
    return waitForVscodeEditorHost();
}

export async function synchronizeLuauLanguageClientProject(
    _files: readonly LuauProjectFile[],
): Promise<void> {
    // The host-side language server reads directly from the project directory.
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
