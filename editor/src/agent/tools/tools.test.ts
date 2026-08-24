import { describe, expect, it } from "vitest";
import type { EditorAgentApi } from "../../types";
import { editorToolRegistry } from "../editor-tool-registry";
import { createEditorTools } from ".";

function createEditor(
    invoke: EditorAgentApi["invoke"],
): EditorAgentApi {
    return {
        capabilities: [],
        invoke,
    };
}

describe("Editor agent tools", () => {
    it("derives the command API and Pi tools from the shared registry", async () => {
        const commands: string[] = [];
        const editor = editorToolRegistry.createAgentApi({
            handler: (command) => async () => {
                commands.push(command);
                return { state: "stopped" };
            },
            requestId: () => "registry-response",
        });

        expect(editor.capabilities).toContain("runtime.status");
        const tool = createEditorTools(editor).find(
            (candidate) => candidate.name === "runtime_status",
        );
        const result = await tool?.execute("tool-call", {});

        expect(commands).toEqual(["runtime.status"]);
        expect(result?.details).toEqual({
            command: "runtime.status",
            value: { state: "stopped" },
        });
    });

    it("forwards project file arguments through the Editor command bus", async () => {
        const requests: unknown[] = [];
        const editor = createEditor(async (request) => {
            requests.push(request);
            return {
                requestId: "response",
                ok: true,
                value: { path: request.path, saved: true },
            };
        });
        const tool = createEditorTools(editor).find(
            (candidate) => candidate.name === "project_write",
        );

        const result = await tool?.execute(
            "tool-call",
            { path: "scripts/main.luau", content: "return {}" },
        );

        expect(requests).toEqual([
            {
                type: "project.write",
                path: "scripts/main.luau",
                content: "return {}",
            },
        ]);
        expect(result?.details).toEqual({
            command: "project.write",
            value: { path: "scripts/main.luau", saved: true },
        });
    });

    it("forwards runtime input and returns viewport images to the agent", async () => {
        const requests: unknown[] = [];
        const editor = createEditor(async (request) => {
            requests.push(request);
            return request.type === "runtime.observe"
                ? {
                      requestId: "response",
                      ok: true,
                      value: {
                          mimeType: "image/png",
                          data: "cG5n",
                          width: 768,
                          height: 432,
                      },
                  }
                : { requestId: "response", ok: true, value: { held: [request.code] } };
        });
        const tools = createEditorTools(editor);

        await tools
            .find((candidate) => candidate.name === "runtime_key")
            ?.execute("key-call", { code: "KeyW", action: "press" });
        const observed = await tools
            .find((candidate) => candidate.name === "runtime_observe")
            ?.execute("observe-call", {});

        expect(requests).toEqual([
            { type: "runtime.key", code: "KeyW", action: "press", durationMs: undefined },
            { type: "runtime.observe" },
        ]);
        expect(observed?.content).toEqual([
            { type: "text", text: "Runtime viewport (768x432)." },
            { type: "image", data: "cG5n", mimeType: "image/png" },
        ]);
    });

    it("routes structured play tools through one runtime inspection command", async () => {
        const requests: unknown[] = [];
        const editor = createEditor(async (request) => {
            requests.push(request);
            return {
                requestId: "response",
                ok: true,
                value: { request_id: "step-response", state: "pending" },
            };
        });
        const tools = createEditorTools(editor);

        await tools
            .find((candidate) => candidate.name === "play_interfaces")
            ?.execute("interfaces-call", {});
        await tools.find((candidate) => candidate.name === "play_step")?.execute(
            "step-call",
            { interface: "game.main", action: { move: "left" }, ticks: 3 },
        );
        await tools
            .find((candidate) => candidate.name === "play_step_status")
            ?.execute("status-call", { requestId: "step-response" });

        expect(requests).toHaveLength(3);
        expect(requests[0]).toEqual({
            type: "runtime.inspect",
            provider: "play.interfaces",
            schema: "play.interfaces.v1",
            payload: {},
        });
        expect(requests[1]).toMatchObject({
            type: "runtime.inspect",
            provider: "play.step",
            schema: "play.step.v1",
            payload: {
                interface: "game.main",
                action: { move: "left" },
                ticks: 3,
            },
        });
        expect((requests[1] as { payload: { request_id: string } }).payload.request_id).toEqual(
            expect.any(String),
        );
        expect(requests[2]).toEqual({
            type: "runtime.inspect",
            provider: "play.step_status",
            schema: "play.step_status.v1",
            payload: { request_id: "step-response" },
        });
    });

    it("surfaces command bus failures as tool errors", async () => {
        const editor = createEditor(async () => ({
            requestId: "response",
            ok: false,
            error: { code: "command_failed", message: "project is not open" },
        }));
        const tool = createEditorTools(editor).find(
            (candidate) => candidate.name === "project_list",
        );

        await expect(tool?.execute("tool-call", {})).rejects.toThrow(
            "project is not open",
        );
    });

    it("does not invoke a command after cancellation", async () => {
        let invoked = false;
        const editor = createEditor(async () => {
            invoked = true;
            return { requestId: "response", ok: true };
        });
        const tool = createEditorTools(editor).find(
            (candidate) => candidate.name === "runtime_restart",
        );
        const controller = new AbortController();
        controller.abort();

        await expect(
            tool?.execute("tool-call", {}, controller.signal),
        ).rejects.toThrow();
        expect(invoked).toBe(false);
    });
});
