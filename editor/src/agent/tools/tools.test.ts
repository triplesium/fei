import { describe, expect, it } from "vitest";
import type { EditorAgentApi } from "../../types";
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
