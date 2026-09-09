import { describe, expect, it, vi } from "vitest";
import {
    controlProfiling,
    createProfileSummaryCatalog,
    inspectCompactProfileSummary,
    inspectProfileFrameArchive,
    inspectProfileFrameDetails,
    inspectProfileFrameHistory,
    mergeProfileFrameHistory,
    parseCompactProfileSummary,
    parseGpuProfileSummary,
    parseProfileCaptureStatus,
    parseProfileFrameArchive,
    parseProfileFrameHistory,
    parseProfileFrameDetails,
    parseProfileSummary,
    profileFrameDetailsToRequest,
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

    it("requests CPU details only for an explicitly selected uncached frame", () => {
        const retainedFrames = new Set([10, 11, 12]);
        const cachedFrames = new Set([10]);

        expect(profileFrameDetailsToRequest(null, retainedFrames, cachedFrames)).toEqual([]);
        expect(profileFrameDetailsToRequest(9, retainedFrames, cachedFrames)).toEqual([]);
        expect(profileFrameDetailsToRequest(10, retainedFrames, cachedFrames)).toEqual([]);
        expect(profileFrameDetailsToRequest(11, retainedFrames, cachedFrames)).toEqual([11]);
    });

    it("merges incremental frame history and replaces discontinuous captures", () => {
        const current = [
            { frame: 8, durationMs: 16 },
            { frame: 9, durationMs: 17 },
        ];
        expect(
            mergeProfileFrameHistory(current, [{ frame: 10, durationMs: 18 }]),
        ).toEqual([...current, { frame: 10, durationMs: 18 }]);
        expect(
            mergeProfileFrameHistory(current, [{ frame: 0, durationMs: 15 }]),
        ).toEqual([{ frame: 0, durationMs: 15 }]);
        expect(mergeProfileFrameHistory(current, [])).toBe(current);
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

    it("reuses compact CPU summary metadata across polls", async () => {
        const catalog = createProfileSummaryCatalog();
        const first = {
            available: true,
            frame_stats: [12, 60, 16.5, 15.8],
            catalog_revision: 2,
            entries: [
                [
                    4,
                    1,
                    7,
                    19,
                    "Update",
                    "wasm-function-index",
                    "wasm:abc",
                    42,
                    "Player::update",
                    "player.cpp",
                    "update",
                    12,
                ],
            ],
            values: [[4, 12, 8, 6, 0.2, 1.1]],
        };
        expect(parseCompactProfileSummary(first, catalog)).toMatchObject({
            frameStats: { frameCount: 12, latestFrameMs: 16.5 },
            systems: [
                {
                    name: "Player::update",
                    count: 12,
                    totalMs: 8,
                    selfMs: 6,
                    meanMs: 8 / 12,
                },
            ],
        });
        expect(catalog.revision).toBe(2);

        const inspect = vi.fn().mockResolvedValue({
            available: true,
            frame_stats: [13, 62, 16, 15.5],
            catalog_revision: 2,
            entries: [],
            values: [[4, 13, 9, 7, 0.2, 1.2]],
        });
        await expect(inspectCompactProfileSummary(inspect, catalog)).resolves.toMatchObject({
            frameStats: { frameCount: 13 },
            systems: [{ name: "Player::update", count: 13, totalMs: 9 }],
        });
        expect(inspect).toHaveBeenCalledWith(
            "profiling.summary_compact",
            "profiling.summary_compact.v1",
            { catalog_revision: 2 },
        );
    });

    it("rejects compact summary values without matching metadata", () => {
        expect(() =>
            parseCompactProfileSummary(
                {
                    available: true,
                    frame_stats: [1, 60, 16, 16],
                    catalog_revision: 1,
                    entries: [],
                    values: [[9, 1, 1, 1, 1, 1]],
                },
                createProfileSummaryCatalog(),
            ),
        ).toThrow(/does not reference a catalog entry/);
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

    it("expands dictionary-encoded frame archives", () => {
        expect(
            parseProfileFrameArchive({
                available: true,
                entries: [
                    [
                        1,
                        7,
                        19,
                        "Update",
                        "wasm-function-index",
                        "wasm:abc",
                        42,
                        "Player::update",
                        "player.cpp",
                        "update",
                        12,
                    ],
                    [0, 0, 0, "", "none", "", 0, "Render", "render.cpp", "draw", 8],
                ],
                frames: [[11, 18.4, [[0, 2, 4, 3, 1, 3], [1, 1, 2, 2, 2, 2]]]],
            }),
        ).toMatchObject({
            available: true,
            details: [
                {
                    frame: 11,
                    durationMs: 18.4,
                    systems: [
                        {
                            systemId: 19,
                            meanMs: 2,
                            selfMeanMs: 1.5,
                            symbol: {
                                kind: "wasm-function-index",
                                moduleId: "wasm:abc",
                                id: 42,
                            },
                        },
                    ],
                    zones: [{ name: "Render", totalMs: 2 }],
                },
            ],
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

    it("requests compact frame archives", async () => {
        const inspect = vi.fn().mockResolvedValue({
            available: true,
            entries: [],
            frames: [[12, 16, []]],
        });

        await expect(inspectProfileFrameArchive(inspect, [12])).resolves.toMatchObject({
            details: [{ frame: 12 }],
        });
        expect(inspect).toHaveBeenCalledWith(
            "profiling.frame_archive",
            "profiling.frame_archive.v1",
            { frames: [12] },
        );
    });

    it("requests only frame history newer than the supplied cursor", async () => {
        const inspect = vi.fn().mockResolvedValue({ available: true, frames: [] });

        await inspectProfileFrameHistory(inspect, 42);
        expect(inspect).toHaveBeenCalledWith(
            "profiling.frame_history",
            "profiling.frame_history.v1",
            { after_frame: 42 },
        );
    });
});
