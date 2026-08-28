import type { ProfileCaptureArchive } from "../runtime/profile-capture-archive";
import {
    inspectGpuProfileSummary,
    inspectProfileFrameDetails,
    inspectProfileFrameHistory,
    inspectProfileSummary,
    type GpuProfileSummary,
    type ProfileFrameDetail,
    type ProfileFrameSample,
    type ProfileSummary,
    type RuntimeInspect,
} from "../runtime/profiling";
import { resolveProfileFrameDetails, resolveProfileSummary } from "./profile-symbols";

export type ProfilerAgentSource =
    | {
          kind: "runtime";
          sessionId: string;
          inspect: RuntimeInspect;
      }
    | {
          kind: "archive";
          archive: ProfileCaptureArchive;
      };

export interface ProfilerCaptureInfo {
    source: "runtime" | "archive";
    sessionId: string;
    finalizedAt: number | null;
    complete: boolean | null;
    warnings: string[];
}

export interface ProfilerAgentSummary {
    capture: ProfilerCaptureInfo;
    cpu: ProfileSummary | null;
    gpu: GpuProfileSummary | null;
}

export interface ProfilerAgentFrames {
    capture: ProfilerCaptureInfo;
    available: boolean;
    frames: ProfileFrameSample[];
}

export interface ProfilerAgentFrame {
    capture: ProfilerCaptureInfo;
    available: boolean;
    frame: ProfileFrameDetail | null;
    message?: string;
}

function captureInfo(source: ProfilerAgentSource): ProfilerCaptureInfo {
    if (source.kind === "runtime") {
        return {
            source: "runtime",
            sessionId: source.sessionId,
            finalizedAt: null,
            complete: null,
            warnings: [],
        };
    }
    return {
        source: "archive",
        sessionId: source.archive.sessionId,
        finalizedAt: source.archive.finalizedAt,
        complete: source.archive.complete,
        warnings: [...source.archive.warnings],
    };
}

function frameLimit(value: number | undefined): number {
    if (value === undefined) return 300;
    if (!Number.isSafeInteger(value) || value < 1 || value > 600) {
        throw new RangeError("Profiler frame limit must be an integer between 1 and 600.");
    }
    return value;
}

function frameNumber(value: number, context: string): number {
    if (!Number.isSafeInteger(value) || value < 0) {
        throw new RangeError(`${context} must be a non-negative integer.`);
    }
    return value;
}

export async function readProfilerSummary(
    source: ProfilerAgentSource,
): Promise<ProfilerAgentSummary> {
    if (source.kind === "archive") {
        return {
            capture: captureInfo(source),
            cpu: source.archive.summary
                ? await resolveProfileSummary(source.archive.summary)
                : null,
            gpu: source.archive.gpuSummary,
        };
    }

    const [cpu, gpu] = await Promise.all([
        inspectProfileSummary(source.inspect).then(resolveProfileSummary),
        inspectGpuProfileSummary(source.inspect),
    ]);
    return { capture: captureInfo(source), cpu, gpu };
}

export async function readProfilerFrames(
    source: ProfilerAgentSource,
    afterFrame?: number,
    limit?: number,
): Promise<ProfilerAgentFrames> {
    const normalizedAfterFrame =
        afterFrame === undefined ? undefined : frameNumber(afterFrame, "Profiler frame cursor");
    const normalizedLimit = frameLimit(limit);
    const history =
        source.kind === "runtime"
            ? await inspectProfileFrameHistory(source.inspect, normalizedAfterFrame ?? null)
            : source.archive.history;
    const frames =
        history?.frames.filter(
            (sample) => normalizedAfterFrame === undefined || sample.frame > normalizedAfterFrame,
        ) ?? [];
    return {
        capture: captureInfo(source),
        available: history?.available ?? false,
        frames: frames.slice(-normalizedLimit),
    };
}

export async function readProfilerFrame(
    source: ProfilerAgentSource,
    requestedFrame: number,
): Promise<ProfilerAgentFrame> {
    const frame = frameNumber(requestedFrame, "Profiler frame");
    const response =
        source.kind === "runtime"
            ? await inspectProfileFrameDetails(source.inspect, [frame])
            : {
                  available: source.archive.history?.available ?? source.archive.details.size > 0,
                  details: source.archive.details.has(frame)
                      ? [source.archive.details.get(frame)!]
                      : [],
              };
    const resolved = await resolveProfileFrameDetails(response);
    const detail = resolved.details[0] ?? null;
    return {
        capture: captureInfo(source),
        available: resolved.available && detail !== null,
        frame: detail,
        ...(detail
            ? {}
            : { message: `CPU details for frame ${frame} are not available in this capture.` }),
    };
}
