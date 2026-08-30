import type { Api, Model } from "@earendil-works/pi-ai";

export interface EditorModelProviderSummary {
    id: string;
    name: string;
    baseUrl: string;
    api: OpenAICompatibleApi;
    configured: boolean;
    models: Model<Api>[];
}

export interface EditorModelSettingsSnapshot {
    active: { providerId: string; modelId: string };
    providers: EditorModelProviderSummary[];
}

export type OpenAICompatibleApi = "responses" | "chat-completions";

export interface OpenAICompatibleModelSettings {
    id: string;
    name: string;
    reasoning: boolean;
    contextWindow: number;
    maxTokens: number;
}

export interface OpenAICompatibleProviderSettings {
    id: string;
    name: string;
    baseUrl: string;
    api: OpenAICompatibleApi;
    models: OpenAICompatibleModelSettings[];
}

export interface EditorModelSettingsUpdate {
    providerId: string;
    modelId: string;
    apiKey?: string;
    provider?: OpenAICompatibleProviderSettings;
}

export interface EditorProviderSettingsUpdate {
    provider: Omit<OpenAICompatibleProviderSettings, "models">;
    apiKey?: string;
}

export interface EditorRegistryModelUpdate {
    providerId: string;
    previousModelId?: string;
    model: OpenAICompatibleModelSettings;
}

export type AgentDensity = "compact" | "comfortable";

export interface EditorSettings {
    version: 1;
    appearance: {
        agentDensity: AgentDensity;
    };
}

function asEditorSettings(value: unknown): EditorSettings {
    if (!value || typeof value !== "object" || Array.isArray(value)) {
        throw new Error("Editor Host returned invalid Editor settings.");
    }
    const settings = value as Partial<EditorSettings>;
    if (
        settings.version !== 1 ||
        !settings.appearance ||
        (settings.appearance.agentDensity !== "compact" &&
            settings.appearance.agentDensity !== "comfortable")
    ) {
        throw new Error("Editor Host returned unsupported Editor settings.");
    }
    return settings as EditorSettings;
}

export interface EditorHostBootstrap {
    version: 1;
    token: string;
    project: {
        open: boolean;
        name?: string;
        rootUri?: string;
    };
    provider: {
        id: string;
        name: string;
        configured: boolean;
        model: Model<Api>;
    };
    modelSettings: EditorModelSettingsSnapshot;
}

export class EditorHostRequestError extends Error {
    constructor(
        message: string,
        readonly status: number,
        readonly code?: string,
    ) {
        super(message);
        this.name = "EditorHostRequestError";
    }
}

function asBootstrap(value: unknown): EditorHostBootstrap {
    if (!value || typeof value !== "object" || Array.isArray(value)) {
        throw new Error("Editor Host returned an invalid bootstrap response.");
    }
    const bootstrap = value as Partial<EditorHostBootstrap>;
    const project = bootstrap.project;
    const provider = bootstrap.provider;
    const model = provider?.model;
    const modelSettings = bootstrap.modelSettings;
    if (
        bootstrap.version !== 1 ||
        typeof bootstrap.token !== "string" ||
        !project ||
        typeof project.open !== "boolean" ||
        (project.name !== undefined && typeof project.name !== "string") ||
        (project.rootUri !== undefined && typeof project.rootUri !== "string") ||
        !provider ||
        typeof provider.id !== "string" ||
        typeof provider.name !== "string" ||
        typeof provider.configured !== "boolean" ||
        !model ||
        typeof model.id !== "string" ||
        typeof model.provider !== "string" ||
        typeof model.api !== "string" ||
        !modelSettings ||
        typeof modelSettings.active?.providerId !== "string" ||
        typeof modelSettings.active?.modelId !== "string" ||
        !Array.isArray(modelSettings.providers) ||
        modelSettings.providers.some(
            (candidate) =>
                !candidate ||
                typeof candidate.id !== "string" ||
                typeof candidate.name !== "string" ||
                typeof candidate.baseUrl !== "string" ||
                (candidate.api !== "responses" && candidate.api !== "chat-completions") ||
                typeof candidate.configured !== "boolean" ||
                !Array.isArray(candidate.models),
        )
    ) {
        throw new Error("Editor Host returned an invalid bootstrap response.");
    }
    return bootstrap as EditorHostBootstrap;
}

async function requestError(response: Response): Promise<EditorHostRequestError> {
    try {
        const value = (await response.json()) as { error?: unknown; code?: unknown };
        return new EditorHostRequestError(
            typeof value.error === "string" ? value.error : `Editor Host error ${response.status}`,
            response.status,
            typeof value.code === "string" ? value.code : undefined,
        );
    } catch {
        return new EditorHostRequestError(`Editor Host error ${response.status}`, response.status);
    }
}

export class EditorHostClient {
    private bootstrapPromise: Promise<EditorHostBootstrap> | undefined;

    bootstrap(refresh = false): Promise<EditorHostBootstrap> {
        if (!this.bootstrapPromise || refresh) {
            const promise = fetch("/api/v1/bootstrap", { cache: "no-store" })
                .then(async (response) => {
                    if (!response.ok) throw await requestError(response);
                    return asBootstrap(await response.json());
                })
                .catch((error) => {
                    if (this.bootstrapPromise === promise) this.bootstrapPromise = undefined;
                    throw error;
                });
            this.bootstrapPromise = promise;
        }
        return this.bootstrapPromise;
    }

    async request(path: string, init: RequestInit = {}): Promise<Response> {
        const bootstrap = await this.bootstrap();
        const headers = new Headers(init.headers);
        headers.set("Authorization", `Bearer ${bootstrap.token}`);
        const response = await fetch(path, { ...init, headers });
        if (!response.ok) throw await requestError(response);
        return response;
    }

    async json<T>(path: string, init: RequestInit = {}): Promise<T> {
        return (await this.request(path, init)).json() as Promise<T>;
    }

    async getEditorSettings(): Promise<EditorSettings> {
        return asEditorSettings(await this.json<unknown>("/api/v1/editor-settings"));
    }

    async updateEditorSettings(settings: EditorSettings): Promise<EditorSettings> {
        return asEditorSettings(
            await this.json<unknown>("/api/v1/editor-settings", {
                method: "PUT",
                headers: { "Content-Type": "application/json" },
                body: JSON.stringify(settings),
            }),
        );
    }

    subscribeEvents(path: string, listener: (value: unknown) => void): () => void {
        const abortController = new AbortController();
        void this.consumeEvents(path, listener, abortController.signal).catch((error) => {
            if (!abortController.signal.aborted) console.warn("[entisium editor] event stream closed", error);
        });
        return () => abortController.abort();
    }

    private async consumeEvents(
        path: string,
        listener: (value: unknown) => void,
        signal: AbortSignal,
    ): Promise<void> {
        const response = await this.request(path, {
            headers: { Accept: "text/event-stream" },
            signal,
        });
        if (!response.body) throw new Error("Editor Host returned an empty event stream.");
        const reader = response.body.pipeThrough(new TextDecoderStream()).getReader();
        let buffer = "";
        while (!signal.aborted) {
            const { done, value } = await reader.read();
            if (done) break;
            buffer += value;
            let boundary = buffer.indexOf("\n\n");
            while (boundary >= 0) {
                const block = buffer.slice(0, boundary);
                buffer = buffer.slice(boundary + 2);
                const data = block
                    .split("\n")
                    .filter((line) => line.startsWith("data:"))
                    .map((line) => line.slice(5).trimStart())
                    .join("\n");
                if (data) listener(JSON.parse(data));
                boundary = buffer.indexOf("\n\n");
            }
        }
    }
}

export const editorHost = new EditorHostClient();
