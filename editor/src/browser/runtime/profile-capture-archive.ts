import {
    controlProfiling,
    inspectGpuProfileSummary,
    inspectProfileFrameArchive,
    inspectProfileFrameHistory,
    inspectProfileSummary,
    type GpuProfileSummary,
    type ProfileFrameDetail,
    type ProfileFrameHistory,
    type ProfileSummary,
    type RuntimeInspect,
} from "./profiling";

const defaultFinalizeTimeoutMilliseconds = 5_000;
const frameDetailBatchSize = 600;

export interface ProfileCaptureArchive {
    sessionId: string;
    summary: ProfileSummary | null;
    history: ProfileFrameHistory | null;
    details: Map<number, ProfileFrameDetail>;
    gpuSummary: GpuProfileSummary | null;
    finalizedAt: number;
    complete: boolean;
    warnings: string[];
}

export interface ProfileCaptureProgress {
    retained: number;
    total: number;
}

interface FinalizeProfileCaptureOptions {
    timeoutMilliseconds?: number;
    now?: () => number;
    onProgress?: (progress: ProfileCaptureProgress) => void;
}

function errorMessage(error: unknown): string {
    return error instanceof Error ? error.message : String(error);
}

function responseIsTooLarge(error: unknown): boolean {
    return /response[_ -]?too[_ -]?large|exceeds the maximum size/i.test(
        errorMessage(error),
    );
}

function timeoutError(): Error {
    return new Error("Profiler capture finalization timed out.");
}

async function withinDeadline<T>(
    operation: Promise<T>,
    deadline: number,
    now: () => number,
): Promise<T> {
    const remaining = deadline - now();
    if (remaining <= 0) throw timeoutError();
    let timer: ReturnType<typeof globalThis.setTimeout> | undefined;
    try {
        return await Promise.race([
            operation,
            new Promise<never>((_, reject) => {
                timer = globalThis.setTimeout(() => reject(timeoutError()), remaining);
            }),
        ]);
    } finally {
        if (timer !== undefined) globalThis.clearTimeout(timer);
    }
}

function detailBatches(frames: readonly number[]): number[][] {
    const batches: number[][] = [];
    for (let offset = 0; offset < frames.length; offset += frameDetailBatchSize) {
        batches.push(frames.slice(offset, offset + frameDetailBatchSize));
    }
    return batches;
}

async function loadDetailBatch(
    inspect: RuntimeInspect,
    frames: number[],
    deadline: number,
    now: () => number,
): Promise<ProfileFrameDetail[]> {
    try {
        return (
            await withinDeadline(inspectProfileFrameArchive(inspect, frames), deadline, now)
        ).details;
    } catch (error) {
        if (!responseIsTooLarge(error) || frames.length === 1) throw error;
        const middle = Math.ceil(frames.length / 2);
        return [
            ...(await loadDetailBatch(
                inspect,
                frames.slice(0, middle),
                deadline,
                now,
            )),
            ...(await loadDetailBatch(
                inspect,
                frames.slice(middle),
                deadline,
                now,
            )),
        ];
    }
}

export async function finalizeProfileCapture(
    inspect: RuntimeInspect,
    sessionId: string,
    options: FinalizeProfileCaptureOptions = {},
): Promise<ProfileCaptureArchive> {
    const now = options.now ?? (() => Date.now());
    const timeoutMilliseconds = Math.max(
        1,
        options.timeoutMilliseconds ?? defaultFinalizeTimeoutMilliseconds,
    );
    const deadline = now() + timeoutMilliseconds;
    const warnings: string[] = [];
    const details = new Map<number, ProfileFrameDetail>();

    try {
        await withinDeadline(controlProfiling(inspect, "stop"), deadline, now);
    } catch (error) {
        warnings.push(`Could not freeze profiler capture: ${errorMessage(error)}`);
    }

    let history: ProfileFrameHistory | null = null;
    try {
        history = await withinDeadline(inspectProfileFrameHistory(inspect), deadline, now);
    } catch (error) {
        warnings.push(`Could not read profiler frame history: ${errorMessage(error)}`);
    }

    let summary: ProfileSummary | null = null;
    let gpuSummary: GpuProfileSummary | null = null;
    const aggregateResults = await Promise.allSettled([
        withinDeadline(inspectProfileSummary(inspect), deadline, now),
        withinDeadline(inspectGpuProfileSummary(inspect), deadline, now),
    ]);
    if (aggregateResults[0].status === "fulfilled") {
        summary = aggregateResults[0].value;
    } else {
        warnings.push(`Could not read profiler summary: ${errorMessage(aggregateResults[0].reason)}`);
    }
    if (aggregateResults[1].status === "fulfilled") {
        gpuSummary = aggregateResults[1].value;
    } else {
        warnings.push(`Could not read GPU profiler summary: ${errorMessage(aggregateResults[1].reason)}`);
    }

    if (history?.available) {
        options.onProgress?.({ retained: 0, total: history.frames.length });
        for (const batch of detailBatches(history.frames.map((frame) => frame.frame))) {
            try {
                const batchDetails = await loadDetailBatch(inspect, batch, deadline, now);
                for (const detail of batchDetails) details.set(detail.frame, detail);
                options.onProgress?.({
                    retained: details.size,
                    total: history.frames.length,
                });
            } catch (error) {
                warnings.push(
                    `Could not retain CPU details for frames ${batch[0]}–${batch.at(-1)}: ${errorMessage(error)}`,
                );
                if (errorMessage(error) === timeoutError().message) break;
            }
        }
    }

    const expectedDetailCount = history?.available ? history.frames.length : 0;
    return {
        sessionId,
        summary,
        history,
        details,
        gpuSummary,
        finalizedAt: now(),
        complete: history !== null && details.size === expectedDetailCount,
        warnings,
    };
}
