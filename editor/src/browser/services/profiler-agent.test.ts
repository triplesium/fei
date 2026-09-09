import { describe, expect, it, vi } from "vitest";
import type { ProfileCaptureArchive } from "../runtime/profile-capture-archive";
import type {
    ProfileEntry,
    ProfileFrameDetail,
    ProfileSummary,
} from "../runtime/profiling";
import {
    readProfilerFrame,
    readProfilerFrames,
    readProfilerSummary,
    type ProfilerAgentSource,
} from "./profiler-agent";

function entry(name: string, totalMs: number): ProfileEntry {
    return {
        scheduleId: 1,
        systemId: 2,
        scheduleName: "Update",
        symbol: null,
        name,
        file: "game.cpp",
        functionName: name,
        line: 10,
        count: 1,
        totalMs,
        selfMs: totalMs,
        meanMs: totalMs,
        selfMeanMs: totalMs,
        minMs: totalMs,
        maxMs: totalMs,
    };
}

function summary(): ProfileSummary {
    return {
        available: true,
        frameStats: {
            available: true,
            frameCount: 3,
            fps: 60,
            latestFrameMs: 18,
            averageFrameMs: 16,
        },
        systems: [entry("Update", 5)],
        zones: [],
    };
}

function detail(frame: number): ProfileFrameDetail {
    return {
        frame,
        durationMs: 31,
        systems: [entry("SpikeSystem", 12)],
        zones: [],
    };
}

describe("profiler agent queries", () => {
    it("reads live summaries, incremental frames, and selected frame details", async () => {
        const inspect = vi.fn(async (provider: string) => {
            if (provider === "profiling.summary") {
                return {
                    available: true,
                    frame_stats: {
                        available: true,
                        frame_count: 3,
                        fps: 60,
                        latest_frame_ms: 18,
                        average_frame_ms: 16,
                    },
                    systems: [
                        {
                            schedule_id: 1,
                            system_id: 2,
                            schedule_name: "Update",
                            symbol_kind: "none",
                            name: "Update",
                            file: "game.cpp",
                            function: "Update",
                            line: 10,
                            count: 1,
                            total_ms: 5,
                            self_ms: 5,
                            mean_ms: 5,
                            self_mean_ms: 5,
                            min_ms: 5,
                            max_ms: 5,
                        },
                    ],
                    zones: [],
                };
            }
            if (provider === "profiling.gpu_summary") {
                return { available: true, entries: [] };
            }
            if (provider === "profiling.frame_history") {
                return {
                    available: true,
                    frames: [
                        { frame: 11, duration_ms: 17 },
                        { frame: 12, duration_ms: 31 },
                    ],
                };
            }
            if (provider === "profiling.frame_detail") {
                return {
                    available: true,
                    details: [
                        {
                            frame: 12,
                            duration_ms: 31,
                            systems: [],
                            zones: [],
                        },
                    ],
                };
            }
            throw new Error(`Unexpected provider: ${provider}`);
        });
        const source: ProfilerAgentSource = {
            kind: "runtime",
            sessionId: "live-session",
            inspect,
        };

        await expect(readProfilerSummary(source)).resolves.toMatchObject({
            capture: { source: "runtime", sessionId: "live-session" },
            cpu: { systems: [{ name: "Update" }] },
            gpu: { available: true },
        });
        await expect(readProfilerFrames(source, 10, 1)).resolves.toMatchObject({
            frames: [{ frame: 12, durationMs: 31 }],
        });
        await expect(readProfilerFrame(source, 12)).resolves.toMatchObject({
            available: true,
            frame: { frame: 12, durationMs: 31 },
        });
        expect(inspect).toHaveBeenCalledWith(
            "profiling.frame_history",
            "profiling.frame_history.v1",
            { after_frame: 10 },
        );
    });

    it("reads retained profiler data after the runtime stops", async () => {
        const retainedDetail = detail(22);
        const archive: ProfileCaptureArchive = {
            sessionId: "stopped-session",
            summary: summary(),
            history: {
                available: true,
                frames: [
                    { frame: 20, durationMs: 16 },
                    { frame: 21, durationMs: 18 },
                    { frame: 22, durationMs: 31 },
                ],
            },
            details: new Map([[22, retainedDetail]]),
            gpuSummary: { available: false, entries: [] },
            finalizedAt: 1234,
            complete: true,
            warnings: [],
        };
        const source: ProfilerAgentSource = { kind: "archive", archive };

        await expect(readProfilerSummary(source)).resolves.toMatchObject({
            capture: {
                source: "archive",
                sessionId: "stopped-session",
                finalizedAt: 1234,
                complete: true,
            },
            cpu: { frameStats: { frameCount: 3 } },
        });
        await expect(readProfilerFrames(source, 20, 1)).resolves.toMatchObject({
            frames: [{ frame: 22, durationMs: 31 }],
        });
        await expect(readProfilerFrame(source, 22)).resolves.toMatchObject({
            available: true,
            frame: { systems: [{ name: "SpikeSystem" }] },
        });
        await expect(readProfilerFrame(source, 21)).resolves.toMatchObject({
            available: false,
            frame: null,
            message: expect.stringContaining("frame 21"),
        });
    });

    it("validates direct command arguments independently of tool schemas", async () => {
        const source: ProfilerAgentSource = {
            kind: "archive",
            archive: {
                sessionId: "capture",
                summary: null,
                history: null,
                details: new Map(),
                gpuSummary: null,
                finalizedAt: 0,
                complete: false,
                warnings: [],
            },
        };

        await expect(readProfilerFrames(source, undefined, 601)).rejects.toThrow(/between 1 and 600/);
        await expect(readProfilerFrame(source, -1)).rejects.toThrow(/non-negative integer/);
    });
});
