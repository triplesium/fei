import { afterEach, beforeEach, expect, it, vi } from "vitest";
import type { AgentRequest, EditorAgentApi } from "../../types";
import { invokePlaytestCommand } from "./playtest-command";

beforeEach(() => vi.useFakeTimers());
afterEach(() => vi.useRealTimers());
const request = (segment = true): AgentRequest => ({
    type: "runtime.inspect", provider: segment ? "play.segment" : "play.step",
    schema: segment ? "play.segment.v1" : "play.step.v1",
    payload: { request_id: "owned", max_ticks: 60 },
});
const ok = (state: string, extra = {}) => ({
    requestId: "response", ok: true,
    value: { request_id: "owned", state, ...extra },
});
function editor(invoke: EditorAgentApi["invoke"]): EditorAgentApi { return { capabilities: [], invoke }; }

it("returns only the consumed terminal result and emits progress without resubmitting", async () => {
    const invoke = vi.fn().mockResolvedValueOnce(ok("pending"))
        .mockResolvedValueOnce(ok("running", { completed_ticks: 10, max_ticks: 60 }))
        .mockResolvedValueOnce(ok("max_ticks", { completed_ticks: 60, observation: { won: false } }));
    const update = vi.fn();
    const result = invokePlaytestCommand(editor(invoke), request(), undefined, update);
    await vi.advanceTimersByTimeAsync(500);
    expect((await result).details.value).toMatchObject({ state: "max_ticks", observation: { won: false } });
    expect(invoke.mock.calls.map(([arg]) => arg.provider)).toEqual(["play.segment", "play.segment_status", "play.segment_status"]);
    expect(update.mock.calls[1]?.[0].content[0].text).toContain("10 / 60 ticks");
});

it("cancels after an abort during submission and consumes cancellation exactly once", async () => {
    const controller = new AbortController();
    const invoke = vi.fn(async (arg: AgentRequest) => {
        if (arg.provider === "play.segment") { controller.abort(); return ok("pending"); }
        return ok("cancelled", { completed_ticks: 2 });
    });
    await expect(invokePlaytestCommand(editor(invoke), request(), controller.signal)).rejects.toThrow("Final segment result");
    expect(invoke.mock.calls.map(([arg]) => arg.provider)).toEqual(["play.segment", "play.segment_cancel"]);
});

it("does not cancel a completion consumed by an in-flight status read", async () => {
    const controller = new AbortController();
    const invoke = vi.fn(async (arg: AgentRequest) => {
        if (arg.provider === "play.segment") return ok("pending");
        controller.abort();
        return ok("stopped");
    });
    const result = expect(invokePlaytestCommand(editor(invoke), request(), controller.signal)).rejects.toThrow();
    await vi.advanceTimersByTimeAsync(250);
    await result;
    expect(invoke).toHaveBeenCalledTimes(2);
});

it("stops the runtime when a single step is aborted", async () => {
    const controller = new AbortController();
    const invoke = vi.fn(async (arg: AgentRequest) => {
        if (arg.provider === "play.step") controller.abort();
        return ok(arg.type === "runtime.stop" ? "stopped" : "pending");
    });
    await expect(invokePlaytestCommand(editor(invoke), request(false), controller.signal)).rejects.toThrow("Runtime stopped");
    expect(invoke.mock.calls[1]?.[0]).toEqual({ type: "runtime.stop" });
});

it("falls back to runtime stop when segment cancellation cannot be confirmed", async () => {
    const controller = new AbortController();
    const invoke = vi.fn(async (arg: AgentRequest) => {
        if (arg.provider === "play.segment") controller.abort();
        if (arg.provider === "play.segment_cancel") throw new Error("disconnected");
        return ok(arg.type === "runtime.stop" ? "stopped" : "pending");
    });
    await expect(invokePlaytestCommand(editor(invoke), request(), controller.signal)).rejects.toThrow("Runtime stopped");
    expect(invoke.mock.calls.at(-1)?.[0].type).toBe("runtime.stop");
});

it("times out, cancels the segment, and reports uncertain execution without retry", async () => {
    const invoke = vi.fn(async (arg: AgentRequest) => ok(arg.provider === "play.segment_cancel" ? "cancelled" : "running"));
    const result = expect(invokePlaytestCommand(editor(invoke), request())).rejects.toThrow("do not retry automatically");
    await vi.advanceTimersByTimeAsync(120_000);
    await result;
    expect(invoke.mock.calls.filter(([arg]) => arg.provider === "play.segment")).toHaveLength(1);
    expect(invoke.mock.calls.at(-1)?.[0].provider).toBe("play.segment_cancel");
});

it("surfaces a consumed failed status without further cancellation or polling", async () => {
    const invoke = vi.fn().mockResolvedValueOnce(ok("pending"))
        .mockResolvedValueOnce(ok("failed", { error: { message: "invalid action" } }));
    const result = expect(invokePlaytestCommand(editor(invoke), request())).rejects.toThrow("invalid action");
    await vi.advanceTimersByTimeAsync(250);
    await result;
    expect(invoke).toHaveBeenCalledTimes(2);
});
