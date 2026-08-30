export type LuauLanguageClientStatus =
    | "stopped"
    | "starting"
    | "ready"
    | "unavailable";

export async function prepareLuauLanguageClient(): Promise<void> {
    // The demo uses Monaco's standalone services and has no language client.
}

export function subscribeLuauLanguageClient(
    listener: (status: LuauLanguageClientStatus) => void,
): () => void {
    listener("stopped");
    return () => undefined;
}

export async function startLuauLanguageClient(_rootUri: string): Promise<void> {
    // The standalone demo has no local host process to bridge to luau-lsp.
}
