import { streamProxy } from "@earendil-works/pi-agent-core";
import {
    editorHost,
    type EditorHostBootstrap,
    type EditorModelSettingsSnapshot,
    type EditorModelSettingsUpdate,
    type EditorProviderSettingsUpdate,
    type EditorRegistryModelUpdate,
} from "../services/editor-host-client";
import type { EditorPiAgent } from "./editor-pi-agent";

export type ModelGatewayState =
    | { state: "connecting" }
    | { state: "unavailable"; error: string }
    | {
          state: "unconfigured";
          providerId: string;
          provider: string;
          modelId: string;
          model: string;
          settings: EditorModelSettingsSnapshot;
      }
    | {
          state: "ready";
          providerId: string;
          provider: string;
          modelId: string;
          model: string;
          settings: EditorModelSettingsSnapshot;
      };

export class EditorModelGateway {
    async connect(agent: EditorPiAgent): Promise<ModelGatewayState> {
        try {
            const bootstrap = await editorHost.bootstrap(true);
            if (!bootstrap.provider.configured) {
                agent.unconfigure();
                return {
                    state: "unconfigured",
                    providerId: bootstrap.provider.id,
                    provider: bootstrap.provider.name,
                    modelId: bootstrap.provider.model.id,
                    model: bootstrap.provider.model.name,
                    settings: bootstrap.modelSettings,
                };
            }
            this.configureAgent(agent, bootstrap);
            return {
                state: "ready",
                providerId: bootstrap.provider.id,
                provider: bootstrap.provider.name,
                modelId: bootstrap.provider.model.id,
                model: bootstrap.provider.model.name,
                settings: bootstrap.modelSettings,
            };
        } catch (error) {
            agent.unconfigure();
            return {
                state: "unavailable",
                error: error instanceof Error ? error.message : String(error),
            };
        }
    }

    async configure(
        settings: EditorModelSettingsUpdate,
        agent: EditorPiAgent,
    ): Promise<ModelGatewayState> {
        await editorHost.request("/api/v1/model-settings", {
            method: "PUT",
            headers: {
                "Content-Type": "application/json",
            },
            body: JSON.stringify(settings),
        });
        return this.connect(agent);
    }

    async removeCredential(providerId: string, agent: EditorPiAgent): Promise<ModelGatewayState> {
        await editorHost.request(
            `/api/v1/model-settings/credential?provider=${encodeURIComponent(providerId)}`,
            { method: "DELETE" },
        );
        return this.connect(agent);
    }

    async refreshModels(providerId: string, force = false): Promise<EditorModelSettingsSnapshot> {
        const response = await editorHost.request(
            `/api/v1/model-settings/refresh?provider=${encodeURIComponent(providerId)}&force=${force}`,
            { method: "POST" },
        );
        return response.json() as Promise<EditorModelSettingsSnapshot>;
    }

    async modelSettings(): Promise<EditorModelSettingsSnapshot> {
        return (await editorHost.request("/api/v1/model-settings")).json() as Promise<EditorModelSettingsSnapshot>;
    }

    async deleteModel(
        providerId: string,
        modelId: string,
        agent: EditorPiAgent,
    ): Promise<ModelGatewayState> {
        await editorHost.request(
            `/api/v1/model-settings/model?provider=${encodeURIComponent(providerId)}&model=${encodeURIComponent(modelId)}`,
            { method: "DELETE" },
        );
        return this.connect(agent);
    }

    async configureProvider(
        settings: EditorProviderSettingsUpdate,
        agent: EditorPiAgent,
    ): Promise<ModelGatewayState> {
        await editorHost.request("/api/v1/model-settings/provider", {
            method: "PUT",
            headers: { "Content-Type": "application/json" },
            body: JSON.stringify(settings),
        });
        return this.connect(agent);
    }

    async configureRegistryModel(
        settings: EditorRegistryModelUpdate,
        agent: EditorPiAgent,
    ): Promise<ModelGatewayState> {
        await editorHost.request("/api/v1/model-settings/model", {
            method: "PUT",
            headers: { "Content-Type": "application/json" },
            body: JSON.stringify(settings),
        });
        return this.connect(agent);
    }

    async deleteProvider(providerId: string, agent: EditorPiAgent): Promise<ModelGatewayState> {
        await editorHost.request(
            `/api/v1/model-settings/provider?provider=${encodeURIComponent(providerId)}`,
            { method: "DELETE" },
        );
        return this.connect(agent);
    }

    private configureAgent(agent: EditorPiAgent, bootstrap: EditorHostBootstrap): void {
        const authToken = bootstrap.token;
        agent.configure({
            model: bootstrap.provider.model,
            reasoning: bootstrap.reasoning,
            streamFn: (model, context, options) =>
                streamProxy(model, context, {
                    ...options,
                    authToken,
                    proxyUrl: location.origin,
                }),
        });
    }
}
