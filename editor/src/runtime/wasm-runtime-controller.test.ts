import { describe, expect, it, vi } from "vitest";
import { WasmRuntimeController } from "./wasm-runtime-controller";
import type { RuntimeSnapshot } from "./types";

describe("WasmRuntimeController agent interaction", () => {
    it("captures the viewport and dispatches held keyboard and pointer input", async () => {
        vi.stubGlobal("location", { origin: "http://localhost" });
        const windowEvents: Array<{ type: string; init: Record<string, unknown> }> = [];
        const canvasEvents: Array<{ type: string; init: Record<string, unknown> }> = [];
        let postedMessage: Record<string, unknown> | undefined;
        class FauxEvent {
            constructor(
                readonly type: string,
                readonly init: Record<string, unknown>,
            ) {}
        }
        const view = {
            KeyboardEvent: FauxEvent,
            MouseEvent: FauxEvent,
            dispatchEvent: (event: { type: string; init: Record<string, unknown> }) => {
                windowEvents.push(event);
                return true;
            },
            postMessage: (message: Record<string, unknown>) => {
                postedMessage = message;
            },
        };
        const canvas = {
            width: 1280,
            height: 720,
            getBoundingClientRect: () => ({ left: 10, top: 20, width: 640, height: 360 }),
            dispatchEvent: (event: { type: string; init: Record<string, unknown> }) => {
                canvasEvents.push(event);
                return true;
            },
        };
        const document = {
            querySelector: () => canvas,
        };
        const controller = new WasmRuntimeController();
        (
            controller as unknown as {
                snapshot: RuntimeSnapshot;
            }
        ).snapshot = {
            state: "running",
            detail: "running",
            script: "loaded",
            frame: "presented",
            session: { channelId: "test", files: [], source: "test" },
        };
        controller.attachFrame({ contentWindow: view, contentDocument: document } as never);

        const capture = controller.capture();
        expect(postedMessage).toMatchObject({ type: "runtime.capture" });
        (
            controller as unknown as {
                onWindowMessage: (event: MessageEvent) => void;
            }
        ).onWindowMessage({
            source: view,
            origin: location.origin,
            data: {
                source: "entisium-runtime",
                channelId: "test",
                type: "runtime.capture",
                requestId: postedMessage?.requestId,
                mimeType: "image/png",
                data: "cG5n",
                width: 768,
                height: 432,
            },
        } as never);
        await expect(capture).resolves.toEqual({
            mimeType: "image/png",
            data: "cG5n",
            width: 768,
            height: 432,
        });

        const inspection = controller.inspect("play.interfaces", "play.interfaces.v1", {});
        expect(postedMessage).toMatchObject({
            type: "runtime.inspect",
            provider: "play.interfaces",
            schema: "play.interfaces.v1",
            payload: {},
        });
        (
            controller as unknown as {
                onWindowMessage: (event: MessageEvent) => void;
            }
        ).onWindowMessage({
            source: view,
            origin: location.origin,
            data: {
                source: "entisium-runtime",
                channelId: "test",
                type: "runtime.inspection",
                requestId: postedMessage?.requestId,
                value: { interfaces: [{ id: "game.main" }] },
            },
        } as never);
        await expect(inspection).resolves.toEqual({
            interfaces: [{ id: "game.main" }],
        });

        await controller.key("KeyW", "press");
        await controller.pointerInput(0.25, 0.5, "click", "left", 0);
        controller.clearInput();

        expect(windowEvents.map((event) => event.type)).toEqual(["keydown", "keyup"]);
        expect(canvasEvents.map((event) => event.type)).toEqual([
            "mousedown",
            "mouseup",
        ]);
        expect(canvasEvents[0]?.init).toMatchObject({ clientX: 170, clientY: 200 });
        vi.unstubAllGlobals();
    });
});
