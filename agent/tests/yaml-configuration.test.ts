import { mkdtemp, readFile, rm, writeFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { afterEach, expect, it, vi } from "vitest";
import { loadHostConfiguration } from "../src/host/configuration.js";
import { HostModelRegistry } from "../src/models/model-registry.js";

const directories: string[] = [];
afterEach(async () => {
    vi.unstubAllEnvs();
    for (const path of directories.splice(0)) await rm(path, { recursive: true, force: true });
});
const source = `version: 1
providers:
  openai:
    apiKey: test-yaml-secret
    models:
      - id: test-chat
        reasoning: true
        contextWindow: 128000
        maxTokens: 4096
agent:
  model: { provider: openai, id: test-chat }
  reasoning: high
imageGeneration:
  model: test-image
  defaults: { size: 1536x1024, quality: low }
runtime:
  executable: auto
`;
async function fixture(text = source) {
    const root = await mkdtemp(join(tmpdir(), "entisium-agent-config-test-"));
    directories.push(root);
    const path = join(root, "config.yaml");
    await writeFile(path, text);
    return { path, host: await loadHostConfiguration(path) };
}

it("uses YAML for model selection and credentials without exposing keys in registry snapshots", async () => {
    const { host } = await fixture();
    const registry = new HostModelRegistry(host.credentials, host.modelSettingsStore);
    expect((await registry.activeModel()).model.id).toBe("test-chat");
    expect(host.agent.reasoning).toBe("high");
    expect(await host.credentials.read("openai")).toEqual({ type: "api_key", key: "test-yaml-secret" });
    expect(JSON.stringify(await registry.snapshot())).not.toContain("test-yaml-secret");
    expect(JSON.stringify(await host.modelSettingsStore.read())).not.toContain("apiKey");
});

it("writes model and key edits into the same YAML while preserving DevKit configuration", async () => {
    const { host, path } = await fixture();
    const registry = new HostModelRegistry(host.credentials, host.modelSettingsStore);
    await registry.configure({ providerId: "openai", modelId: "test-chat", apiKey: "updated-yaml-key" });
    await registry.configureProvider({ provider: { id: "custom", name: "Custom", api: "responses", baseUrl: "https://example.com/v1" }, apiKey: "custom-test-key" });
    const updated = await host.store.read();
    expect(updated.providers.openai.apiKey).toBe("updated-yaml-key");
    expect(updated.providers.custom.apiKey).toBe("custom-test-key");
    expect(updated.imageGeneration.model).toBe("test-image");
    expect(updated.agent).toMatchObject({ reasoning: "high" });
    expect(await readFile(path, "utf8")).toContain("updated-yaml-key");
    await registry.deleteCredential("openai");
    expect(await host.credentials.read("openai")).toBeUndefined();
    expect(await host.credentials.list()).toEqual([{ providerId: "custom", type: "api_key" }]);
});

it("validates Agent configuration and explicitly selected paths instead of falling back", async () => {
    const { path } = await fixture();
    await expect(loadHostConfiguration(`${path}.missing`)).rejects.toThrow("does not exist");
    await writeFile(path, source.replace("reasoning: high", "reasoning: secret-invalid-value"));
    await expect(loadHostConfiguration(path)).rejects.toThrow("agent settings");
    await writeFile(path, source.replace("id: test-chat }", "id: missing }"));
    await expect(loadHostConfiguration(path)).rejects.toThrow("must reference");
});

it("gives explicit paths priority over ETS_CONFIG_PATH", async () => {
    const { path } = await fixture();
    vi.stubEnv("ETS_CONFIG_PATH", `${path}.missing`);
    expect((await loadHostConfiguration(path)).config?.version).toBe(1);
    await expect(loadHostConfiguration()).rejects.toThrow("does not exist");
});

it("can delete the active model when an image-only provider comes first", async () => {
    const reordered = source.replace('  openai:\n', '  images:\n    baseUrl: https://api.openai.com/v1\n    apiKey: image-only-key\n  openai:\n');
    const { host } = await fixture(reordered);
    const registry = new HostModelRegistry(host.credentials, host.modelSettingsStore);
    await registry.configureRegistryModel({ providerId: "openai", model: {
        id: "other-chat", name: "Other chat", reasoning: false, contextWindow: 128000, maxTokens: 4096,
    } });
    expect((await registry.deleteModel("openai", "test-chat")).active).toEqual({ providerId: "openai", modelId: "other-chat" });
});
