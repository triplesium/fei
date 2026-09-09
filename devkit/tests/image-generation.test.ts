import { mkdtemp, readFile, rm, writeFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { afterEach, expect, it, vi } from "vitest";
import { ImageGenerationService } from "../src/image-generation/service.js";
import { HostProjectService } from "../src/workspace/project-service.js";

const png = Buffer.from("iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+jRZkAAAAASUVORK5CYII=", "base64");
const cleanup: (() => Promise<void>)[] = [];
afterEach(async () => { for (const dispose of cleanup.splice(0)) await dispose(); });
async function fixture() {
    const root = await mkdtemp(join(tmpdir(), "entisium-image-test-"));
    await writeFile(join(root, "project.yaml"), "name: Test\n");
    const project = new HostProjectService(root);
    cleanup.push(async () => { project.dispose(); await rm(root, { recursive: true, force: true }); });
    const request = vi.fn<typeof fetch>().mockResolvedValue(Response.json({ data: [{ b64_json: png.toString("base64") }] }));
    const resolveApiKey = vi.fn(async () => "test-key");
    const service = new ImageGenerationService(project, { model: "test-image-model", resolveApiKey, fetch: request });
    return { root, project, request, resolveApiKey, service };
}
const input = { prompt: "A game icon", path: "assets/generated/icon.png" };

it("calls the configured model and saves a PNG with metadata, without base64 in the result", async () => {
    const { root, service, request } = await fixture();
    expect(await service.generate(input)).toEqual({ path: input.path, mimeType: "image/png", width: 1, height: 1, bytes: png.length, model: "test-image-model" });
    expect(await readFile(join(root, input.path))).toEqual(png);
    expect(request).toHaveBeenCalledOnce();
    const [url, options] = request.mock.calls[0];
    expect(url).toBe("https://api.openai.com/v1/images/generations");
    expect(JSON.parse(options!.body as string)).toMatchObject({ model: "test-image-model", n: 1, size: "1024x1024", output_format: "png" });
});

it("rejects unsafe paths, missing credentials and existing output before contacting OpenAI", async () => {
    const { service, project, request, resolveApiKey } = await fixture();
    for (const path of ["../out.png", "assets/../out.png", "C:/out.png", "project.yaml", "assets/a.png:stream"]) {
        await expect(service.generate({ ...input, path })).rejects.toThrow();
    }
    resolveApiKey.mockResolvedValue("");
    await expect(service.generate(input)).rejects.toThrow("API key");
    await project.write(input.path, "existing");
    await expect(service.generate(input)).rejects.toThrow("already exists");
    expect(request).not.toHaveBeenCalled();
});

it("never replaces an asset created while generation is running", async () => {
    const { service, project, request } = await fixture();
    request.mockImplementation(async () => {
        await project.write(input.path, "concurrent asset");
        return Response.json({ data: [{ b64_json: png.toString("base64") }] });
    });
    await expect(service.generate(input)).rejects.toThrow();
    expect((await project.read(input.path))?.toString()).toBe("concurrent asset");
});

it("propagates cancellation and does not save a response received after cancellation", async () => {
    const { service, project, request } = await fixture();
    const controller = new AbortController();
    request.mockImplementation(async (_url, options) => {
        controller.abort();
        expect(options!.signal!.aborted).toBe(true);
        return Response.json({ data: [{ b64_json: png.toString("base64") }] });
    });
    await expect(service.generate(input, controller.signal)).rejects.toThrow();
    expect(await project.exists(input.path)).toBe(false);
});

it("rejects invalid output and does not retry provider errors or expose upstream text", async () => {
    const { service, project, request } = await fixture();
    request.mockResolvedValueOnce(new Response("secret upstream text", { status: 429 }));
    await expect(service.generate(input)).rejects.toThrow("HTTP 429");
    expect(request).toHaveBeenCalledTimes(1);
    for (const body of [{}, { data: [{ b64_json: "bad!" }] }, { data: [{ b64_json: Buffer.from("not png").toString("base64") }] }]) {
        request.mockResolvedValueOnce(Response.json(body));
        await expect(service.generate(input)).rejects.toThrow();
    }
    expect(await project.exists(input.path)).toBe(false);
});

it("handles multi-megabyte image payloads without overflowing the base64 validator", async () => {
    const { service, request } = await fixture();
    const large = Buffer.concat([png, Buffer.alloc(4 * 1024 * 1024)]);
    request.mockResolvedValueOnce(Response.json({ data: [{ b64_json: large.toString("base64") }] }));
    expect(await service.generate(input)).toMatchObject({ bytes: large.length });
});

it("uses YAML image defaults and lets explicit tool arguments override them", async () => {
    const { project, request } = await fixture();
    const service = new ImageGenerationService(project, {
        model: "configured-image", resolveApiKey: async () => "test-key", fetch: request,
        defaults: { size: "1536x1024", quality: "low" },
    });
    await service.generate(input);
    expect(JSON.parse(request.mock.calls[0][1]!.body as string)).toMatchObject({ size: "1536x1024", quality: "low" });
    request.mockResolvedValueOnce(Response.json({ data: [{ b64_json: png.toString("base64") }] }));
    await service.generate({ ...input, path: "assets/other.png", size: "1024x1536", quality: "high" });
    expect(JSON.parse(request.mock.calls[1][1]!.body as string)).toMatchObject({ size: "1024x1536", quality: "high" });
});

it.each(["https://images.example.com/proxy/v1", "https://images.example.com/proxy/v1/"])("posts to the configured base URL %s with the resolved key", async (baseUrl) => {
    const { project, request } = await fixture();
    const service = new ImageGenerationService(project, {
        baseUrl, model: "custom-image", resolveApiKey: async () => "custom-key", fetch: request,
    });
    await service.generate(input);
    expect(request.mock.calls[0][0]).toBe("https://images.example.com/proxy/v1/images/generations");
    expect(request.mock.calls[0][1]!.headers).toMatchObject({ Authorization: "Bearer custom-key" });
});

it("rejects invalid provider URLs before resolving credentials or sending requests", async () => {
    const { project, request, resolveApiKey } = await fixture();
    for (const baseUrl of ["not-a-url", "file:///secret", "https://user:secret@example.com", "https://example.com?key=secret", "https://example.com#secret"]) {
        const service = new ImageGenerationService(project, { baseUrl, model: "custom-image", resolveApiKey, fetch: request });
        await expect(service.generate(input)).rejects.toThrow("baseUrl");
    }
    expect(request).not.toHaveBeenCalled();
    expect(resolveApiKey).not.toHaveBeenCalled();
});

it.each(["https://openrouter.ai/api/v1", "https://router.example.com/proxy/v1/"])("uses the OpenRouter Image API at %s and saves its PNG response", async (baseUrl) => {
    const { project, request, root } = await fixture();
    request.mockResolvedValueOnce(Response.json({ data: [{ b64_json: png.toString("base64"), media_type: "image/png" }], usage: { cost: 0.01 } }));
    const service = new ImageGenerationService(project, {
        api: "openrouter-images", baseUrl, model: "openai/gpt-image-2", fetch: request,
        resolveApiKey: async () => "router-test-key",
    });
    expect(await service.generate({ ...input, quality: "low" })).toMatchObject({ path: input.path, mimeType: "image/png", width: 1, height: 1 });
    expect(request).toHaveBeenCalledOnce();
    expect(request.mock.calls[0][0]).toBe(`${baseUrl.replace(/\/+$/, "")}/images`);
    const options = request.mock.calls[0][1]!;
    expect(options.headers).toMatchObject({ Authorization: "Bearer router-test-key" });
    expect(JSON.parse(options.body as string)).toEqual({ model: "openai/gpt-image-2", prompt: input.prompt, resolution: "1K", aspect_ratio: "1:1", quality: "low", n: 1, output_format: "png" });
    expect(await readFile(join(root, input.path))).toEqual(png);
});

it("rejects non-PNG OpenRouter output and HTTP errors without protocol fallback", async () => {
    const { project, request } = await fixture();
    const service = new ImageGenerationService(project, { api: "openrouter-images", model: "test-image", fetch: request, resolveApiKey: async () => "router-test-key" });
    request.mockResolvedValueOnce(Response.json({ data: [{ b64_json: png.toString("base64"), media_type: "image/webp" }] }));
    await expect(service.generate(input)).rejects.toThrow("PNG");
    expect(await project.exists(input.path)).toBe(false);
    request.mockResolvedValueOnce(new Response("upstream private body", { status: 502 }));
    await expect(service.generate(input)).rejects.toThrow("HTTP 502");
    expect(request).toHaveBeenCalledTimes(2);
    expect(request.mock.calls.every(([url]) => url === "https://openrouter.ai/api/v1/images")).toBe(true);
});

it("sends unified OpenRouter options and rejects incompatible OpenAI options before network access", async () => {
    const { project, request, resolveApiKey } = await fixture();
    const openai = new ImageGenerationService(project, { model: "test", fetch: request, resolveApiKey });
    await expect(openai.generate({ ...input, seed: 123 })).rejects.toThrow("seed requires");
    expect(request).not.toHaveBeenCalled();
    expect(resolveApiKey).not.toHaveBeenCalled();
    const router = new ImageGenerationService(project, { api: "openrouter-images", model: "test", fetch: request, resolveApiKey });
    await router.generate({ ...input, resolution: "2K", aspect_ratio: "16:9", background: "transparent", seed: 123 });
    expect(JSON.parse(request.mock.calls[0][1]!.body as string)).toMatchObject({ resolution: "2K", aspect_ratio: "16:9", background: "transparent", seed: 123 });
    expect(JSON.parse(request.mock.calls[0][1]!.body as string)).not.toHaveProperty("size");
});
