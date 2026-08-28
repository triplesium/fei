import { describe, expect, it } from "vitest";
import {
    finalizeProfileCapture,
    type ProfileCaptureArchive,
} from "./profile-capture-archive";
import type { RuntimeInspect } from "./profiling";

function profileSummary() {
    return {
        available: true,
        frame_stats: {
            available: true,
            frame_count: 3,
            fps: 60,
            latest_frame_ms: 16,
            average_frame_ms: 16,
        },
        systems: [],
        zones: [],
    };
}

function captureStatus() {
    return {
        available: true,
        recording: false,
        bounded: false,
        frame_limit: 0,
        frames_remaining: 0,
    };
}

function profileInspect(
    frames: number[],
    detailResponse: (requested: number[]) => unknown,
    calls: Array<{ provider: string; payload: unknown }>,
): RuntimeInspect {
    return async (provider, _schema, payload) => {
        calls.push({ provider, payload });
        if (provider === "profiling.control") return captureStatus();
        if (provider === "profiling.summary") return profileSummary();
        if (provider === "profiling.gpu_summary") {
            return { available: true, entries: [] };
        }
        if (provider === "profiling.frame_history") {
            return {
                available: true,
                frames: frames.map((frame) => ({ frame, duration_ms: 16 + frame })),
            };
        }
        if (provider === "profiling.frame_archive") {
            return detailResponse(
                (payload as { frames: number[] }).frames,
            );
        }
        throw new Error(`Unexpected provider: ${provider}`);
    };
}

function detailPayload(frames: number[]) {
    return {
        available: true,
        entries: [],
        frames: frames.map((frame) => [frame, 16 + frame, []]),
    };
}

function retainedFrames(archive: ProfileCaptureArchive): number[] {
    return [...archive.details.keys()].sort((left, right) => left - right);
}

describe("profile capture archive", () => {
    it("freezes the capture before retaining every frame detail", async () => {
        const calls: Array<{ provider: string; payload: unknown }> = [];
        const inspect = profileInspect(
            [1, 2, 3],
            (frames) => detailPayload(frames),
            calls,
        );

        const archive = await finalizeProfileCapture(inspect, "session-1");

        expect(calls[0]).toEqual({
            provider: "profiling.control",
            payload: { action: "stop" },
        });
        expect(archive.sessionId).toBe("session-1");
        expect(archive.complete).toBe(true);
        expect(archive.warnings).toEqual([]);
        expect(retainedFrames(archive)).toEqual([1, 2, 3]);
    });

    it("splits detail batches when a response exceeds the inspection limit", async () => {
        const calls: Array<{ provider: string; payload: unknown }> = [];
        const inspect = profileInspect(
            [1, 2, 3],
            (frames) => {
                if (frames.length > 1) {
                    throw new Error("response_too_large: response exceeds the maximum size");
                }
                return detailPayload(frames);
            },
            calls,
        );

        const archive = await finalizeProfileCapture(inspect, "session-2");
        const detailRequests = calls
            .filter((call) => call.provider === "profiling.frame_archive")
            .map((call) => (call.payload as { frames: number[] }).frames);

        expect(archive.complete).toBe(true);
        expect(retainedFrames(archive)).toEqual([1, 2, 3]);
        expect(detailRequests).toEqual([[1, 2, 3], [1, 2], [1], [2], [3]]);
    });

    it("returns a usable partial archive when detail retention fails", async () => {
        const calls: Array<{ provider: string; payload: unknown }> = [];
        const inspect = profileInspect(
            [7, 8],
            () => {
                throw new Error("transport closed");
            },
            calls,
        );

        const archive = await finalizeProfileCapture(inspect, "session-3");

        expect(archive.complete).toBe(false);
        expect(archive.details.size).toBe(0);
        expect(archive.history?.frames.map((frame) => frame.frame)).toEqual([7, 8]);
        expect(archive.warnings).toEqual([
            "Could not retain CPU details for frames 7–8: transport closed",
        ]);
    });

    it("bounds finalization time when the runtime stops responding", async () => {
        const calls: Array<{ provider: string; payload: unknown }> = [];
        const inspect = profileInspect(
            [4],
            () => new Promise<never>(() => undefined),
            calls,
        );

        const archive = await finalizeProfileCapture(inspect, "session-4", {
            timeoutMilliseconds: 10,
        });

        expect(archive.complete).toBe(false);
        expect(archive.history?.frames).toHaveLength(1);
        expect(archive.warnings.at(-1)).toContain("finalization timed out");
    });
});
