import { fileURLToPath } from "node:url";
import { spawn } from "node:child_process";
import { createServer } from "node:http";
import { mkdtemp, readFile, rm, writeFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join, resolve } from "node:path";
import { expect, it } from "vitest";
import { EncryptedCredentialStore } from "../src/models/credential-store.js";

// Exercises the actual process entry and model HTTP transport with a local scripted
// provider. No account credentials, paid model calls, Editor or browser are used.
it.skipIf(!process.env.ETS_RUNTIME_MCP_TEST_EXE || process.platform !== "win32")("runs the CLI against a local model fixture and saves a real capture", async () => {
    const directory = await mkdtemp(join(tmpdir(), "entisium-agent-cli-"));
    const settings = join(directory, "models.json");
    const credentials = join(directory, "credentials.json");
    const calls = ["native_runtime_play", "native_play_interfaces", "native_runtime_capture"];
    let requests = 0;
    const provider = createServer(async (request, response) => {
        for await (const _chunk of request) { /* consume the request before replying */ }
        const index = requests++;
        response.writeHead(200, { "Content-Type": "text/event-stream" });
        const delta = index < calls.length
            ? { role: "assistant", tool_calls: [{ index: 0, id: `call-${index}`, type: "function", function: { name: calls[index], arguments: "{}" } }] }
            : { role: "assistant", content: "CLI finished." };
        const chunk = (value: object, reason: string | null) => ({ id: `completion-${index}`, object: "chat.completion.chunk", created: 1, model: "fixture", choices: [{ index: 0, delta: value, finish_reason: reason }] });
        response.write(`data: ${JSON.stringify(chunk(delta, null))}\n\n`);
        response.write(`data: ${JSON.stringify(chunk({}, index < calls.length ? "tool_calls" : "stop"))}\n\n`);
        response.end("data: [DONE]\n\n");
    });
    await new Promise<void>((done) => provider.listen(0, "127.0.0.1", done));
    const port = (provider.address() as { port: number }).port;
    let child: ReturnType<typeof spawn> | undefined;
    let output = "";
    let error = "";
    let gamePid: number | undefined;
    try {
        await writeFile(settings, JSON.stringify({ version: 2, active: { providerId: "fixture", modelId: "fixture" }, providers: [{
            id: "fixture", name: "Fixture", baseUrl: `http://127.0.0.1:${port}/v1`, api: "chat-completions",
            models: [{ id: "fixture", name: "Fixture", reasoning: false, contextWindow: 100000, maxTokens: 4096 }],
        }] }));
        await new EncryptedCredentialStore(credentials).modify("fixture", async () => ({ type: "api_key", key: "local-fixture-only" }));
        child = spawn(process.execPath, ["--import", "tsx", fileURLToPath(new URL("../src/cli/main.ts", import.meta.url)), "--project", fileURLToPath(new URL("../../samples/projects/skyline_strike/project.yaml", import.meta.url)), "--prompt", "Start and capture the native game.", "--output", directory], {
            windowsHide: true, stdio: ["ignore", "pipe", "pipe"],
            env: { ...process.env, OPENAI_LOG: "off", NO_PROXY: "*", ETS_EDITOR_MODEL: "", ETS_EDITOR_MODEL_SETTINGS_PATH: settings, ETS_EDITOR_CREDENTIAL_PATH: credentials, ETS_RUNTIME_HOST_PATH: process.env.ETS_RUNTIME_MCP_TEST_EXE },
        });
        child.stdout!.on("data", (chunk) => { output += chunk.toString(); });
        child.stderr!.on("data", (chunk) => { error += chunk.toString(); });
        const code = await new Promise<number | null>((done, fail) => { child!.once("error", fail); child!.once("exit", done); });
        expect(code, `requests=${requests}\n` + error + output).toBe(0);
        const events = output.trim().split("\n").map((line) => JSON.parse(line));
        const started = events.find((event) => event.type === "tool_execution_end" && event.tool === "native_runtime_play");
        gamePid = JSON.parse(started.content[0].text).pid;
        expect(events.some((event) => event.isError)).toBe(false);
        expect(events).toContainEqual({ type: "assistant", text: "CLI finished." });
        const artifact = events.find((event) => event.type === "artifact");
        expect((await readFile(artifact.path)).subarray(1, 4).toString()).toBe("PNG");
        expect(() => process.kill(gamePid!, 0)).toThrow();
        expect(requests).toBe(4);
    } finally {
        if (child && child.exitCode === null && child.signalCode === null) child.kill();
        if (gamePid) { try { process.kill(gamePid); } catch { /* already cleaned by CLI */ } }
        provider.closeAllConnections(); await new Promise<void>((done) => provider.close(() => done()));
        await rm(directory, { recursive: true, force: true });
    }
}, 60_000);
