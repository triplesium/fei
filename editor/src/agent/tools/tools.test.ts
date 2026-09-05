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
        expect(editor.capabilities).toContain("profiler.summary");
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

    it("starts agent runtimes in playtest mode by default", async () => {
        const requests: unknown[] = [];
        const editor = createEditor(async (request) => {
            requests.push(request);
            return {
                requestId: "response",
                ok: true,
                value: { state: "starting", mode: request.mode },
            };
        });
        const runtimePlay = createEditorTools(editor).find(
            (candidate) => candidate.name === "runtime_play",
        );

        await runtimePlay?.execute("play-call", {});
        await runtimePlay?.execute("interactive-call", {
            mode: "interactive",
        });

        expect(requests).toEqual([
            { type: "runtime.play", mode: "playtest" },
            { type: "runtime.play", mode: "interactive" },
        ]);
    });

    it("routes structured play tools through one runtime inspection command", async () => {
        const requests: unknown[] = [];
        let stepId: unknown;
        const editor = createEditor(async (request) => {
            requests.push(request);
            if (request.provider === "play.step") stepId = (request.payload as { request_id: string }).request_id;
            return {
                requestId: "response",
                ok: true,
                value: { request_id: stepId, state: request.provider === "play.step_status" ? "completed" : "pending" },
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
        expect(tools.find((candidate) => candidate.name === "play_step_status")).toBeUndefined();

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
            payload: { request_id: stepId },
        });
    });

    it("routes reactive play segments and cancellation", async () => {
        const requests: unknown[] = [];
        let segmentId: unknown;
        const editor = createEditor(async (request) => {
            requests.push(request);
            if (request.provider === "play.segment") segmentId = (request.payload as { request_id: string }).request_id;
            return { requestId: "response", ok: true, value: { request_id: segmentId, state: request.provider === "play.segment" ? "pending" : "stopped" } };
        });
        const tools = createEditorTools(editor);

        await tools.find((candidate) => candidate.name === "play_segment")?.execute(
            "segment-call",
            {
                interface: "game.main",
                source: "return function(ctx) return { stop = 'done' } end",
                maxTicks: 30,
            },
        );
        expect(tools.find((candidate) => candidate.name === "play_segment_status")).toBeUndefined();
        await tools
            .find((candidate) => candidate.name === "play_segment_cancel")
            ?.execute("segment-cancel", { requestId: "segment-1" });

        expect(requests[0]).toMatchObject({
            type: "runtime.inspect",
            provider: "play.segment",
            schema: "play.segment.v1",
            payload: {
                interface: "game.main",
                max_ticks: 30,
            },
        });
        expect(requests[1]).toEqual({
            type: "runtime.inspect",
            provider: "play.segment_status",
            schema: "play.segment_status.v1",
            payload: { request_id: segmentId },
        });
        expect(requests[2]).toEqual({
            type: "runtime.inspect",
            provider: "play.segment_cancel",
            schema: "play.segment_cancel.v1",
            payload: { request_id: "segment-1" },
        });
    });

    it("exposes profiler overview, frame history, and frame detail tools", async () => {
        const requests: unknown[] = [];
        const editor = createEditor(async (request) => {
            requests.push(request);
            return { requestId: "response", ok: true, value: { available: true } };
        });
        const tools = createEditorTools(editor);

        await tools
            .find((candidate) => candidate.name === "profiler_summary")
            ?.execute("summary-call", {});
        await tools
            .find((candidate) => candidate.name === "profiler_frames")
            ?.execute("frames-call", { afterFrame: 120, limit: 60 });
        await tools
            .find((candidate) => candidate.name === "profiler_frame")
            ?.execute("frame-call", { frame: 144 });

        expect(requests).toEqual([
            { type: "profiler.summary" },
            { type: "profiler.frames", afterFrame: 120, limit: 60 },
            { type: "profiler.frame", frame: 144 },
        ]);
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
