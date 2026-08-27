export interface ProfileFrameStats {
    available: boolean;
    frameCount: number;
    fps: number;
    latestFrameMs: number;
    averageFrameMs: number;
}

export interface ProfileEntry {
    scheduleId: number;
    systemId: number;
    scheduleName: string;
    symbol: ProfileSymbolReference | null;
    name: string;
    file: string;
    functionName: string;
    line: number;
    count: number;
    totalMs: number;
    selfMs: number;
    meanMs: number;
    selfMeanMs: number;
    minMs: number;
    maxMs: number;
}

export interface ProfileSymbolReference {
    kind: "pe-rva" | "wasm-function-index";
    moduleId: string;
    id: number;
}

export interface ProfileSummary {
    available: boolean;
    frameStats: ProfileFrameStats;
    systems: ProfileEntry[];
    zones: ProfileEntry[];
}

export interface ProfileFrameSample {
    frame: number;
    durationMs: number;
}

export interface ProfileFrameHistory {
    available: boolean;
    frames: ProfileFrameSample[];
}

export interface ProfileFrameDetail {
    frame: number;
    durationMs: number;
    systems: ProfileEntry[];
    zones: ProfileEntry[];
}

export interface ProfileFrameDetails {
    available: boolean;
    details: ProfileFrameDetail[];
}

export interface GpuProfileEntry {
    name: string;
    count: number;
    latestMs: number;
    totalMs: number;
    meanMs: number;
    minMs: number;
    maxMs: number;
}

export interface GpuProfileSummary {
    available: boolean;
    entries: GpuProfileEntry[];
}

export interface ProfileCaptureStatus {
    available: boolean;
    recording: boolean;
    bounded: boolean;
    frameLimit: number;
    framesRemaining: number;
}

export type ProfileControlAction = "status" | "start" | "capture" | "stop" | "clear";
export type RuntimeInspect = (provider: string, schema: string, payload: unknown) => Promise<unknown>;

export function startsNewProfileSession(
    previousSessionId: string | null,
    sessionId: string | null,
): boolean {
    return sessionId !== null && sessionId !== previousSessionId;
}

const providers = {
    summary: ["profiling.summary", "profiling.summary.v1"],
    frames: ["profiling.frame_history", "profiling.frame_history.v1"],
    frameDetails: ["profiling.frame_detail", "profiling.frame_detail.v1"],
    gpu: ["profiling.gpu_summary", "profiling.gpu_summary.v1"],
    control: ["profiling.control", "profiling.control.v1"],
} as const;

function objectValue(value: unknown, context: string): Record<string, unknown> {
    if (!value || typeof value !== "object" || Array.isArray(value)) {
        throw new TypeError(`${context} must be an object.`);
    }
    return value as Record<string, unknown>;
}

function booleanValue(value: unknown, context: string): boolean {
    if (typeof value !== "boolean") throw new TypeError(`${context} must be a boolean.`);
    return value;
}

function numberValue(value: unknown, context: string): number {
    if (typeof value !== "number" || !Number.isFinite(value)) {
        throw new TypeError(`${context} must be a finite number.`);
    }
    return value;
}

function stringValue(value: unknown, context: string): string {
    if (typeof value !== "string") throw new TypeError(`${context} must be a string.`);
    return value;
}

function arrayValue(value: unknown, context: string): unknown[] {
    if (!Array.isArray(value)) throw new TypeError(`${context} must be an array.`);
    return value;
}

function profileEntry(value: unknown, context: string): ProfileEntry {
    const entry = objectValue(value, context);
    const symbolKind =
        entry.symbol_kind === undefined
            ? "none"
            : stringValue(entry.symbol_kind, `${context}.symbol_kind`);
    if (
        symbolKind !== "none" &&
        symbolKind !== "pe-rva" &&
        symbolKind !== "wasm-function-index"
    ) {
        throw new TypeError(`${context}.symbol_kind is not supported.`);
    }
    return {
        scheduleId: numberValue(entry.schedule_id, `${context}.schedule_id`),
        systemId:
            entry.system_id === undefined
                ? 0
                : numberValue(entry.system_id, `${context}.system_id`),
        scheduleName: stringValue(entry.schedule_name, `${context}.schedule_name`),
        symbol:
            symbolKind === "none"
                ? null
                : {
                      kind: symbolKind,
                      moduleId: stringValue(entry.symbol_module, `${context}.symbol_module`),
                      id: numberValue(entry.symbol_id, `${context}.symbol_id`),
                  },
        name: stringValue(entry.name, `${context}.name`),
        file: stringValue(entry.file, `${context}.file`),
        functionName: stringValue(entry.function, `${context}.function`),
        line: numberValue(entry.line, `${context}.line`),
        count: numberValue(entry.count, `${context}.count`),
        totalMs: numberValue(entry.total_ms, `${context}.total_ms`),
        selfMs: numberValue(entry.self_ms, `${context}.self_ms`),
        meanMs: numberValue(entry.mean_ms, `${context}.mean_ms`),
        selfMeanMs: numberValue(entry.self_mean_ms, `${context}.self_mean_ms`),
        minMs: numberValue(entry.min_ms, `${context}.min_ms`),
        maxMs: numberValue(entry.max_ms, `${context}.max_ms`),
    };
}

export function parseProfileSummary(value: unknown): ProfileSummary {
    const summary = objectValue(value, "Profiling summary");
    const stats = objectValue(summary.frame_stats, "Profiling summary.frame_stats");
    return {
        available: booleanValue(summary.available, "Profiling summary.available"),
        frameStats: {
            available: booleanValue(stats.available, "Profiling summary.frame_stats.available"),
            frameCount: numberValue(stats.frame_count, "Profiling summary.frame_stats.frame_count"),
            fps: numberValue(stats.fps, "Profiling summary.frame_stats.fps"),
            latestFrameMs: numberValue(
                stats.latest_frame_ms,
                "Profiling summary.frame_stats.latest_frame_ms",
            ),
            averageFrameMs: numberValue(
                stats.average_frame_ms,
                "Profiling summary.frame_stats.average_frame_ms",
            ),
        },
        systems: arrayValue(summary.systems, "Profiling summary.systems").map((entry, index) =>
            profileEntry(entry, `Profiling summary.systems[${index}]`),
        ),
        zones: arrayValue(summary.zones, "Profiling summary.zones").map((entry, index) =>
            profileEntry(entry, `Profiling summary.zones[${index}]`),
        ),
    };
}

export function parseProfileFrameHistory(value: unknown): ProfileFrameHistory {
    const history = objectValue(value, "Profiling frame history");
    return {
        available: booleanValue(history.available, "Profiling frame history.available"),
        frames: arrayValue(history.frames, "Profiling frame history.frames").map((value, index) => {
            const frame = objectValue(value, `Profiling frame history.frames[${index}]`);
            return {
                frame: numberValue(frame.frame, `Profiling frame history.frames[${index}].frame`),
                durationMs: numberValue(
                    frame.duration_ms,
                    `Profiling frame history.frames[${index}].duration_ms`,
                ),
            };
        }),
    };
}

export function parseProfileFrameDetails(value: unknown): ProfileFrameDetails {
    const response = objectValue(value, "Profiling frame details");
    return {
        available: booleanValue(response.available, "Profiling frame details.available"),
        details: arrayValue(response.details, "Profiling frame details.details").map(
            (value, index) => {
                const detail = objectValue(value, `Profiling frame details.details[${index}]`);
                return {
                    frame: numberValue(
                        detail.frame,
                        `Profiling frame details.details[${index}].frame`,
                    ),
                    durationMs: numberValue(
                        detail.duration_ms,
                        `Profiling frame details.details[${index}].duration_ms`,
                    ),
                    systems: arrayValue(
                        detail.systems,
                        `Profiling frame details.details[${index}].systems`,
                    ).map((entry, entryIndex) =>
                        profileEntry(
                            entry,
                            `Profiling frame details.details[${index}].systems[${entryIndex}]`,
                        ),
                    ),
                    zones: arrayValue(
                        detail.zones,
                        `Profiling frame details.details[${index}].zones`,
                    ).map((entry, entryIndex) =>
                        profileEntry(
                            entry,
                            `Profiling frame details.details[${index}].zones[${entryIndex}]`,
                        ),
                    ),
                };
            },
        ),
    };
}

export function parseGpuProfileSummary(value: unknown): GpuProfileSummary {
    const summary = objectValue(value, "GPU profiling summary");
    return {
        available: booleanValue(summary.available, "GPU profiling summary.available"),
        entries: arrayValue(summary.entries, "GPU profiling summary.entries").map(
            (value, index) => {
                const entry = objectValue(value, `GPU profiling summary.entries[${index}]`);
                return {
                    name: stringValue(entry.name, `GPU profiling summary.entries[${index}].name`),
                    count: numberValue(entry.count, `GPU profiling summary.entries[${index}].count`),
                    latestMs: numberValue(
                        entry.latest_ms,
                        `GPU profiling summary.entries[${index}].latest_ms`,
                    ),
                    totalMs: numberValue(
                        entry.total_ms,
                        `GPU profiling summary.entries[${index}].total_ms`,
                    ),
                    meanMs: numberValue(
                        entry.mean_ms,
                        `GPU profiling summary.entries[${index}].mean_ms`,
                    ),
                    minMs: numberValue(
                        entry.min_ms,
                        `GPU profiling summary.entries[${index}].min_ms`,
                    ),
                    maxMs: numberValue(
                        entry.max_ms,
                        `GPU profiling summary.entries[${index}].max_ms`,
                    ),
                };
            },
        ),
    };
}

export function parseProfileCaptureStatus(value: unknown): ProfileCaptureStatus {
    const status = objectValue(value, "Profiling capture status");
    return {
        available: booleanValue(status.available, "Profiling capture status.available"),
        recording: booleanValue(status.recording, "Profiling capture status.recording"),
        bounded: booleanValue(status.bounded, "Profiling capture status.bounded"),
        frameLimit: numberValue(status.frame_limit, "Profiling capture status.frame_limit"),
        framesRemaining: numberValue(
            status.frames_remaining,
            "Profiling capture status.frames_remaining",
        ),
    };
}

export async function inspectProfileSummary(inspect: RuntimeInspect): Promise<ProfileSummary> {
    return parseProfileSummary(await inspect(...providers.summary, {}));
}

export async function inspectProfileFrameHistory(
    inspect: RuntimeInspect,
): Promise<ProfileFrameHistory> {
    return parseProfileFrameHistory(await inspect(...providers.frames, {}));
}

export async function inspectProfileFrameDetails(
    inspect: RuntimeInspect,
    frames: number[],
): Promise<ProfileFrameDetails> {
    return parseProfileFrameDetails(await inspect(...providers.frameDetails, { frames }));
}

export async function inspectGpuProfileSummary(
    inspect: RuntimeInspect,
): Promise<GpuProfileSummary> {
    return parseGpuProfileSummary(await inspect(...providers.gpu, {}));
}

export async function controlProfiling(
    inspect: RuntimeInspect,
    action: ProfileControlAction,
    frames?: number,
): Promise<ProfileCaptureStatus> {
    const payload = action === "capture" ? { action, frames } : { action };
    return parseProfileCaptureStatus(await inspect(...providers.control, payload));
}
