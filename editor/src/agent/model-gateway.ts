import { streamProxy } from "@earendil-works/pi-agent-core";
import { editorHost, type EditorHostBootstrap } from "../services/editor-host-client";
import type { EditorPiAgent } from "./editor-pi-agent";

export type ModelGatewayState =
    | { state: "connecting" }
    | { state: "unavailable"; error: string }
    | { state: "unconfigured"; provider: string; model: string }
    | { state: "ready"; provider: string; model: string };

export class EditorModelGateway {
    async connect(agent: EditorPiAgent): Promise<ModelGatewayState> {
        try {
            const bootstrap = await editorHost.bootstrap(true);
            if (!bootstrap.provider.configured) {
                agent.unconfigure();
                return {
                    state: "unconfigured",
                    provider: bootstrap.provider.name,
                    model: bootstrap.provider.model.name,
                };
            }
            this.configureAgent(agent, bootstrap);
            return {
                state: "ready",
                provider: bootstrap.provider.name,
                model: bootstrap.provider.model.name,
            };
        } catch (error) {
            agent.unconfigure();
            return {
                state: "unavailable",
                error: error instanceof Error ? error.message : String(error),
            };
        }
    }

    async saveDeepSeekApiKey(apiKey: string, agent: EditorPiAgent): Promise<ModelGatewayState> {
        const key = apiKey.trim();
        if (!key) throw new Error("DeepSeek API key is required.");
        await editorHost.request("/api/v1/credentials/deepseek", {
            method: "PUT",
            headers: {
                "Content-Type": "application/json",
            },
            body: JSON.stringify({ apiKey: key }),
        });
        return this.connect(agent);
    }

    async removeDeepSeekApiKey(agent: EditorPiAgent): Promise<ModelGatewayState> {
        await editorHost.request("/api/v1/credentials/deepseek", { method: "DELETE" });
        return this.connect(agent);
    }

    private configureAgent(agent: EditorPiAgent, bootstrap: EditorHostBootstrap): void {
        const authToken = bootstrap.token;
        agent.configure({
            model: bootstrap.provider.model,
            streamFn: (model, context, options) =>
                streamProxy(model, context, {
                    ...options,
                    authToken,
                    proxyUrl: location.origin,
                }),
        });
    }
}
