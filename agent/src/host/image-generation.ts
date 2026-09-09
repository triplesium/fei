import type { CredentialStore } from "@earendil-works/pi-ai";
import { ImageGenerationService } from "@entisium/devkit/image-generation/service";
import type { HostProjectService } from "@entisium/devkit/workspace/project-service";
import type { EntisiumConfig } from "@entisium/devkit/settings/config";

/** Shared Node host composition for the CLI and Editor server. */
export function createHostImageGeneration(project: HostProjectService, credentials: CredentialStore, config?: EntisiumConfig) {
    const provider = config?.imageGeneration.provider ?? "openai";
    const settings = config && Object.hasOwn(config.providers, provider) ? config.providers[provider] : undefined;
    if (provider !== "openai" && !settings?.baseUrl) throw new Error("The image generation provider requires baseUrl in config.yaml.");
    return new ImageGenerationService(project, {
        baseUrl: settings?.baseUrl,
        api: config?.imageGeneration.api,
        model: process.env.ETS_IMAGE_MODEL?.trim() || process.env.ENTISIUM_IMAGE_MODEL?.trim() || config?.imageGeneration.model || "gpt-image-2",
        timeoutMs: config?.imageGeneration.timeoutMs,
        defaults: config?.imageGeneration.defaults,
        resolveApiKey: async (signal) => {
            const key = provider === "openai" ? process.env.OPENAI_API_KEY?.trim() : undefined;
            if (key) return key;
            const credential = await credentials.read(provider, { signal });
            return credential?.type === "api_key" ? credential.key : undefined;
        },
    });
}
