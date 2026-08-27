import { describe, expect, it, vi } from "vitest";
import {
    controlProfiling,
    inspectProfileFrameDetails,
    parseGpuProfileSummary,
    parseProfileCaptureStatus,
    parseProfileFrameHistory,
    parseProfileFrameDetails,
    parseProfileSummary,
    startsNewProfileSession,
} from "./profiling";

describe("runtime profiling protocol", () => {
    it("retains a stopped session and resets only for a new runtime session", () => {
        expect(startsNewProfileSession(null, null)).toBe(false);
        expect(startsNewProfileSession(null, "session-a")).toBe(true);
        expect(startsNewProfileSession("session-a", null)).toBe(false);
        expect(startsNewProfileSession("session-a", "session-a")).toBe(false);
        expect(startsNewProfileSession("session-a", "session-b")).toBe(true);
    });

    it("parses CPU summaries and frame history", () => {
        expect(
            parseProfileSummary({
                available: true,
                frame_stats: {
                    available: true,
                    frame_count: 12,
                    fps: 60,
                    latest_frame_ms: 16.5,
                    average_frame_ms: 15.8,
                },
                systems: [
                    {
                        schedule_id: 7,
                        system_id: 19,
                        schedule_name: "Update",
                        symbol_kind: "wasm-function-index",
                        symbol_module: "wasm:abc",
                        symbol_id: 42,
                        name: "scripts/player.luau::update",
                        file: "scripts/player.luau",
                        function: "update",
                        line: 42,
                        count: 12,
                        total_ms: 8,
                        self_ms: 6,
                        mean_ms: 0.67,
                        self_mean_ms: 0.5,
                        min_ms: 0.2,
                        max_ms: 1.1,
                    },
                ],
                zones: [],
            }),
        ).toMatchObject({
            available: true,
            frameStats: { frameCount: 12, latestFrameMs: 16.5 },
            systems: [
                {
                    scheduleId: 7,
                    systemId: 19,
                    symbol: {
                        kind: "wasm-function-index",
                        moduleId: "wasm:abc",
                        id: 42,
                    },
                    name: "scripts/player.luau::update",
                    functionName: "update",
                    selfMs: 6,
                },
            ],
        });

        expect(
            parseProfileFrameHistory({
                available: true,
                frames: [
                    { frame: 10, duration_ms: 16.2 },
                    { frame: 11, duration_ms: 18.4 },
                ],
            }),
        ).toEqual({
            available: true,
            frames: [
                { frame: 10, durationMs: 16.2 },
                { frame: 11, durationMs: 18.4 },
            ],
        });

        expect(
            parseProfileFrameDetails({
                available: true,
                details: [
                    {
                        frame: 11,
                        duration_ms: 18.4,
                        systems: [
                            {
                                schedule_id: 7,
                                schedule_name: "Update",
                                name: "Player::update",
                                file: "player.cpp",
                                function: "update",
                                line: 12,
                                count: 2,
                                total_ms: 4,
                                self_ms: 3,
                                mean_ms: 2,
                                self_mean_ms: 1.5,
                                min_ms: 1,
                                max_ms: 3,
                            },
                        ],
                        zones: [],
                    },
                ],
            }),
        ).toMatchObject({
            available: true,
            details: [
                {
                    frame: 11,
                    durationMs: 18.4,
                    systems: [{ name: "Player::update", selfMs: 3 }],
                },
            ],
        });
    });

    it("parses GPU entries and capture state", () => {
        expect(
            parseGpuProfileSummary({
                available: true,
                entries: [
                    {
                        name: "Render/Main",
                        count: 3,
                        latest_ms: 2.5,
                        total_ms: 7,
                        mean_ms: 2.33,
                        min_ms: 2,
                        max_ms: 2.5,
                    },
                ],
            }),
        ).toMatchObject({ entries: [{ name: "Render/Main", latestMs: 2.5 }] });

        expect(
            parseProfileCaptureStatus({
                available: true,
                recording: true,
                bounded: true,
                frame_limit: 300,
                frames_remaining: 120,
            }),
        ).toEqual({
            available: true,
            recording: true,
            bounded: true,
            frameLimit: 300,
            framesRemaining: 120,
        });
    });

    it("rejects malformed responses and sends bounded capture requests", async () => {
        expect(() =>
            parseProfileFrameHistory({ available: true, frames: [{ frame: "bad" }] }),
        ).toThrow(/finite number/);

        const inspect = vi.fn().mockResolvedValue({
            available: true,
            recording: true,
            bounded: true,
            frame_limit: 300,
            frames_remaining: 300,
        });
        await expect(controlProfiling(inspect, "capture", 300)).resolves.toMatchObject({
            recording: true,
            frameLimit: 300,
        });
        expect(inspect).toHaveBeenCalledWith(
            "profiling.control",
            "profiling.control.v1",
            { action: "capture", frames: 300 },
        );
    });

    it("requests frame details in batches", async () => {
        const inspect = vi.fn().mockResolvedValue({ available: true, details: [] });

        await expect(inspectProfileFrameDetails(inspect, [12, 13])).resolves.toEqual({
            available: true,
            details: [],
        });
        expect(inspect).toHaveBeenCalledWith(
            "profiling.frame_detail",
            "profiling.frame_detail.v1",
            { frames: [12, 13] },
        );
    });
});
