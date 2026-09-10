import { openaiProvider } from "@earendil-works/pi-ai/providers/openai";
import { openrouterProvider } from "@earendil-works/pi-ai/providers/openrouter";
import type { MetadataTarget, ModelMetadata } from "./metadata.js";

const openai = openaiProvider();
const openrouter = openrouterProvider();
export function builtinMetadata(target: MetadataTarget, id: string): ModelMetadata | undefined {
    const known = (target.type === "openai" ? openai : target.type === "openrouter" ? openrouter : undefined)?.getModels().find((model) => model.id === id);
    if (known) return { id, name: known.name, inputModalities: [...known.input],
        chat: { contextWindow: known.contextWindow, maxOutputTokens: known.maxTokens, reasoning: known.reasoning } };
    if (target.baseUrl?.replace(/\/+$/, "") === "https://api.deepseek.com" && ["deepseek-v4-flash", "deepseek-v4-pro"].includes(id)) {
        return { id, name: id === "deepseek-v4-flash" ? "DeepSeek V4 Flash" : "DeepSeek V4 Pro", inputModalities: ["text"], outputModalities: ["text"],
            chat: { contextWindow: 1_000_000, maxOutputTokens: 384_000, reasoning: true } };
    }
    return undefined;
}
