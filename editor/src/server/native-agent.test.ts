import { fileURLToPath } from "node:url";
import { mkdir, writeFile } from "node:fs/promises";
import { dirname, resolve } from "node:path";
import { expect, it } from "vitest";
import { createModels, fauxAssistantMessage, fauxProvider, fauxToolCall } from "@earendil-works/pi-ai";
import type { AgentEvent } from "@earendil-works/pi-agent-core";
import { NativeRuntime } from "@entisium/devkit/runtime/native-runtime";
import { RuntimeSession } from "@entisium/devkit/runtime/session";
import { createEditorHost } from "./server.js";
import { EncryptedCredentialStore } from "@entisium/agent/models/credential-store";
import { EditorPiAgent } from "../browser/agent/editor-pi-agent.js";
import { createNativeAgentTools } from "@entisium/agent/tools/runtime";
import { runAgentTask } from "@entisium/agent/core/task";

it.skipIf(!process.env.ETS_RUNTIME_MCP_TEST_EXE).each(["direct", "editor"])("runs the shared agent through %s tools against a real native game", async (mode) => {
    const project = fileURLToPath(new URL("../../../samples/projects/skyline_strike/project.yaml", import.meta.url));
    const runtime = new NativeRuntime(process.env.ETS_RUNTIME_MCP_TEST_EXE!);
    const session = new RuntimeSession(runtime, async () => project);
    const host = mode === "editor" ? createEditorHost({
        credentials: new EncryptedCredentialStore(), distDirectory: ".", runtimeDirectory: ".", port: 0,
        projectDirectory: dirname(project), nativeSession: session,
    }) : undefined;
    const address = await host?.listen();
    const base = address ? `http://${address.host}:${address.port}` : "";
    const tools = createNativeAgentTools(host ? async (name, parameters) => {
        const response = await fetch(`${base}/api/v1/native-runtime`, {
            method: "POST", headers: { Authorization: `Bearer ${host.token}`, "Content-Type": "application/json" },
            body: JSON.stringify({ name, parameters }),
        });
        if (!response.ok) throw new Error(await response.text());
        return response.json();
    } : session.invoke.bind(session));
    const calls: [string, object][] = [
        ["runtime_play", {}], ["play_interfaces", {}],
        ["play_observe", { interface: "skyline-strike.main" }],
        ["play_step", { interface: "skyline-strike.main", action: { move_x: 1, move_y: 0, fire: true }, ticks: 3 }],
        ["runtime_capture", {}],
        ["play_observe", { interface: "skyline-strike.main" }],
    ];
    const faux = fauxProvider();
    faux.setResponses([...calls.map(([name, input]) => fauxAssistantMessage(fauxToolCall(`native_${name}`, input), { stopReason: "toolUse" })), fauxAssistantMessage("Finished.")]);
    const models = createModels(); models.setProvider(faux.provider);
    const configuration = { model: faux.getModel(), streamFn: models.streamSimple.bind(models) };
    const results: any[] = [];
    const onEvent = async (event: AgentEvent) => {
        if (event.type !== "tool_execution_end") return;
        expect(event.isError, JSON.stringify(event.result)).toBe(false);
        results.push(JSON.parse(event.result.content.find((part: any) => part.type === "text").text));
        const image = event.result.content.find((part: any) => part.type === "image");
        if (image) {
            const output = fileURLToPath(new URL("../../../build/agent-smoke", import.meta.url)); await mkdir(output, { recursive: true });
            const png = Buffer.from(image.data, "base64"); expect(png.subarray(1, 4).toString()).toBe("PNG");
            await writeFile(resolve(output, `${mode}.png`), png);
        }
    };
    let editor: EditorPiAgent | undefined;
    try {
        if (mode === "editor") {
            editor = new EditorPiAgent({ capabilities: [], invoke: async () => { throw new Error("Browser command must not run"); } }, tools);
            editor.configure(configuration); editor.subscribe(onEvent); await editor.prompt("Play and capture the native game.");
        } else await runAgentTask({ prompt: "Play and capture the native game.", configuration, tools, onEvent, cleanup: () => session.stop() });
        expect(results).toHaveLength(calls.length);
        expect(JSON.stringify(results[1])).toContain("skyline-strike.main");
        expect(results[3].observation.player.x).toBeGreaterThan(results[2].observation.player.x);
        expect(results[3].frame).toBe(results[2].frame + 3);
        expect(results[5].frame).toBe(results[3].frame);
    } finally {
        editor?.dispose(); await session.stop();
        if (host) { host.server.closeAllConnections(); await new Promise<void>((done) => host.server.close(() => done())); }
    }
    expect(runtime.status().state).toBe("stopped");
    expect(() => process.kill(results[0].pid, 0)).toThrow();
}, 60_000);
