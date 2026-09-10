import { mkdtemp, readFile, readdir, rm } from "node:fs/promises";
import { join } from "node:path";
import { tmpdir } from "node:os";
import { afterEach, expect, it, vi } from "vitest";
import { ModelMetadataService } from "../src/models/service.js";
import type { MetadataTarget } from "../src/models/metadata.js";

const target: MetadataTarget = { providerId: "router", type: "openrouter", apiKey: "private-test-api-key" };
const directories: string[] = [];
afterEach(async () => { for (const path of directories.splice(0)) await rm(path, { recursive: true, force: true }); });
const response = (value: unknown) => new Response(JSON.stringify(value), { headers: { "Content-Type": "application/json" } });
const catalog = (context = 64000) => ({ data: [{ id: "vendor/chat", name: "Remote chat",
    context_length: context, architecture: { input_modalities: ["text", "image"], output_modalities: ["text"] },
    supported_parameters: ["tools", "reasoning"], top_provider: { context_length: 32000, max_completion_tokens: 8000 } }] });

it("merges remote fields with partial overrides and preserves primary-route scope", async () => {
    const request = vi.fn(async () => response(catalog()));
    const service = new ModelMetadataService({ fetch: request, cacheDirectory: null, now: () => 1000 });
    const result = await service.getModel(target, "vendor/chat", { override: { id: "vendor/chat", chat: { reasoning: false } } });
    expect(result.model).toMatchObject({ name: "Remote chat", chat: { contextWindow: 64000, tools: true, reasoning: false },
        topProvider: { contextWindow: 32000, maxOutputTokens: 8000 }, inputModalities: ["text", "image"] });
    expect(result.model.chat?.maxOutputTokens).toBeUndefined();
    expect(result.sources["chat.reasoning"].source).toBe("override");
    expect(result.sources["chat.contextWindow"]).toEqual({ source: "remote", fetchedAt: 1000 });
    expect(result.status).toBe("found");
    expect(request.mock.calls[0][0].toString()).toBe("https://openrouter.ai/api/v1/models?output_modalities=all");
    expect(request.mock.calls[0][1]).toMatchObject({ headers: { Authorization: "Bearer private-test-api-key" }, redirect: "error" });
});

it("distinguishes unknown models, incomplete facts, malformed responses and unsupported discovery", async () => {
    const request = vi.fn(async () => response({ data: [{ id: "vendor/minimal" }] }));
    const service = new ModelMetadataService({ fetch: request, cacheDirectory: null });
    expect((await service.getModel(target, "vendor/minimal")).model.chat?.reasoning).toBeUndefined();
    expect((await service.getModel(target, "unlisted"))).toMatchObject({ model: { id: "unlisted" }, status: "not-found" });
    expect((await service.getModel({ ...target, type: "openai-compatible" }, "custom"))).toMatchObject({ status: "unsupported", model: { id: "custom" } });
    expect(request).toHaveBeenCalledTimes(1);
    const bad = new ModelMetadataService({ cacheDirectory: null, fetch: async () => response({ secret: "do-not-echo" }) });
    expect(await bad.getModel(target, "unlisted")).toMatchObject({ status: "unavailable", model: { id: "unlisted" } });
});

it("uses OpenAI discovery without inventing capabilities and supplements bundled metadata", async () => {
    const request = vi.fn(async () => response({ data: [{ id: "new-openai-model", owned_by: "openai" }, { id: "gpt-4o" }] }));
    const service = new ModelMetadataService({ fetch: request, cacheDirectory: null });
    const openai: MetadataTarget = { providerId: "account", type: "openai", apiKey: "openai-test-key" };
    expect((await service.getModel(openai, "new-openai-model")).model).toEqual({ id: "new-openai-model" });
    expect((await service.getModel(openai, "gpt-4o")).sources["chat.contextWindow"].source).toBe("builtin");
    expect((await service.getModel({ ...openai, apiKey: undefined }, "gpt-4o")).status).toBe("unavailable");
    expect(request).toHaveBeenCalledTimes(1);
});

it("queries fal endpoints and schemas on demand, retaining schema documents and cursors", async () => {
    const openapi = { openapi: "3.0.0", paths: { "/run": { post: {
        requestBody: { content: { "application/json": { schema: { type: "object", required: ["prompt"] } } } },
        responses: { "200": { content: { "application/json": { schema: { type: "object" } } } } },
    } } } };
    const request = vi.fn(async () => response({ models: [{ endpoint_id: "vendor/image", metadata: { display_name: "Image", category: "text-to-image" }, openapi }], next_cursor: "page-two" }));
    const service = new ModelMetadataService({ fetch: request, cacheDirectory: null });
    const fal: MetadataTarget = { providerId: "fal-account", type: "fal", apiKey: "fal-test-key" };
    const result = await service.getModel(fal, "vendor/image", { includeSchema: true });
    expect(result.model).toMatchObject({ outputModalities: ["image"], inputSchema: { required: ["prompt"] }, outputSchema: { type: "object" }, openapi });
    const url = new URL(request.mock.calls[0][0]);
    expect(url.searchParams.get("endpoint_id")).toBe("vendor/image");
    expect(url.searchParams.get("expand")).toBe("openapi-3.0");
    expect(request.mock.calls[0][1]?.headers).toEqual({ Authorization: "Key fal-test-key" });
    expect((await service.listModels(fal, { cursor: "page-one" })).nextCursor).toBe("page-two");
    expect(new URL(request.mock.calls[1][0]).searchParams.has("expand")).toBe(false);
});

it("persists isolated cache entries without credentials and keeps old facts on refresh failure", async () => {
    const directory = await mkdtemp(join(tmpdir(), "entisium-metadata-")); directories.push(directory);
    let now = 1000;
    const request = vi.fn(async () => response(catalog()));
    const service = new ModelMetadataService({ fetch: request, cacheDirectory: directory, now: () => now, ttlMs: 100 });
    await service.getModel(target, "vendor/chat");
    const offline = vi.fn(async () => new Response(null, { status: 503 }));
    const restored = new ModelMetadataService({ fetch: offline, cacheDirectory: directory, now: () => now, ttlMs: 100 });
    expect((await restored.getModel(target, "vendor/chat")).model.chat?.contextWindow).toBe(64000);
    expect(offline).not.toHaveBeenCalled();
    now = 1200;
    expect(await restored.getModel(target, "vendor/chat", { force: true })).toMatchObject({ stale: true, status: "unavailable", model: { chat: { contextWindow: 64000 } } });
    expect((await restored.getModel({ ...target, apiKey: "different-key" }, "vendor/chat")).model.chat?.contextWindow).toBeUndefined();
    expect((await restored.getModel({ ...target, baseUrl: "https://different.example/v1" }, "vendor/chat")).model.chat?.contextWindow).toBeUndefined();
    const files = await readdir(directory);
    expect(files.every((file) => /^[a-f0-9]{64}\.json$/.test(file))).toBe(true);
    expect((await Promise.all(files.map((file) => readFile(join(directory, file), "utf8")))).join()).not.toContain(target.apiKey);
});

it("coalesces refreshes, isolates cancellation, and does not mutate returned snapshots", async () => {
    let complete!: (response: Response) => void;
    const request = vi.fn(() => new Promise<Response>((resolve) => { complete = resolve; }));
    const service = new ModelMetadataService({ fetch: request, cacheDirectory: null });
    const abort = new AbortController();
    const first = service.getModel(target, "vendor/chat", { signal: abort.signal });
    const second = service.getModel(target, "vendor/chat");
    const rejected = expect(first).rejects.toThrow("cancel first");
    await vi.waitFor(() => expect(request).toHaveBeenCalledTimes(1));
    abort.abort(new Error("cancel first"));
    await rejected;
    complete(response(catalog()));
    const saved = await second;
    request.mockImplementationOnce(async () => response(catalog(128000)));
    expect((await service.getModel(target, "vendor/chat", { force: true })).model.chat?.contextWindow).toBe(128000);
    expect(saved.model.chat?.contextWindow).toBe(64000);
});

it("bounds failed cold lookups with a timeout and backs off subsequent requests", async () => {
    const request = vi.fn((_url: unknown, init?: RequestInit) => new Promise<Response>((_resolve, reject) => {
        init?.signal?.addEventListener("abort", () => reject(new Error("private upstream detail")), { once: true });
    }));
    const service = new ModelMetadataService({ fetch: request, cacheDirectory: null, timeoutMs: 10 });
    expect((await service.getModel(target, "vendor/chat")).status).toBe("unavailable");
    expect((await service.getModel(target, "vendor/chat")).status).toBe("unavailable");
    expect(request).toHaveBeenCalledTimes(1);
});

it("returns stale data immediately while one background refresh updates the cache", async () => {
    let now = 1000;
    let complete!: (response: Response) => void;
    const request = vi.fn(async () => response(catalog()));
    const service = new ModelMetadataService({ fetch: request, cacheDirectory: null, now: () => now, ttlMs: 100 });
    await service.getModel(target, "vendor/chat");
    now = 1200;
    request.mockImplementationOnce(() => new Promise<Response>((resolve) => { complete = resolve; }));
    const stale = await service.getModel(target, "vendor/chat");
    expect(stale).toMatchObject({ stale: true, model: { chat: { contextWindow: 64000 } } });
    expect((await service.getModel(target, "vendor/chat")).stale).toBe(true);
    expect(request).toHaveBeenCalledTimes(2);
    complete(response(catalog(128000)));
    await vi.waitFor(async () => expect((await service.getModel(target, "vendor/chat")).stale).toBe(false));
    expect((await service.getModel(target, "vendor/chat")).model.chat?.contextWindow).toBe(128000);
    expect(stale.model.chat?.contextWindow).toBe(64000);
});
