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

interface ProfileSummaryCatalogEntry {
    system: boolean;
    entry: ProfileEntry;
}

export interface ProfileSummaryCatalog {
    revision: number;
    entries: Map<number, ProfileSummaryCatalogEntry>;
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

export function profileFrameDetailsToRequest(
    selectedFrame: number | null,
    retainedFrames: { has(frame: number): boolean },
    cachedFrames: { has(frame: number): boolean },
): number[] {
    if (
        selectedFrame === null ||
        !retainedFrames.has(selectedFrame) ||
        cachedFrames.has(selectedFrame)
    ) {
        return [];
    }
    return [selectedFrame];
}

export function mergeProfileFrameHistory(
    current: ProfileFrameSample[],
    update: readonly ProfileFrameSample[],
    capacity = 600,
): ProfileFrameSample[] {
    if (update.length === 0) return current;
    const currentLast = current.at(-1)?.frame;
    const updateFirst = update[0]!.frame;
    const merged =
        currentLast !== undefined && updateFirst === currentLast + 1
            ? [...current, ...update]
            : [...update];
    return merged.slice(-capacity);
}

const providers = {
    summary: ["profiling.summary", "profiling.summary.v1"],
    summaryCompact: ["profiling.summary_compact", "profiling.summary_compact.v1"],
    frames: ["profiling.frame_history", "profiling.frame_history.v1"],
    frameDetails: ["profiling.frame_detail", "profiling.frame_detail.v1"],
    frameArchive: ["profiling.frame_archive", "profiling.frame_archive.v1"],
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

function nonNegativeIntegerValue(value: unknown, context: string): number {
    const result = numberValue(value, context);
    if (!Number.isSafeInteger(result) || result < 0) {
        throw new TypeError(`${context} must be a non-negative integer.`);
    }
    return result;
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

export function createProfileSummaryCatalog(): ProfileSummaryCatalog {
    return { revision: 0, entries: new Map() };
}

function compactSummaryCatalogEntry(
    value: unknown,
    context: string,
): [number, ProfileSummaryCatalogEntry] {
    const fields = arrayValue(value, context);
    if (fields.length !== 12) throw new TypeError(`${context} must contain 12 fields.`);
    const index = nonNegativeIntegerValue(fields[0], `${context}[0]`);
    const metadata = profileArchiveEntry(fields.slice(1), context);
    return [index, metadata];
}

export function parseCompactProfileSummary(
    value: unknown,
    catalog: ProfileSummaryCatalog,
): ProfileSummary {
    const summary = objectValue(value, "Compact profiling summary");
    const revision = nonNegativeIntegerValue(
        summary.catalog_revision,
        "Compact profiling summary.catalog_revision",
    );
    if (revision === 0) {
        throw new TypeError("Compact profiling summary.catalog_revision must be positive.");
    }

    const metadata = arrayValue(summary.entries, "Compact profiling summary.entries");
    let activeEntries = catalog.entries;
    if (revision !== catalog.revision || metadata.length > 0) {
        activeEntries = new Map();
        for (const [metadataIndex, value] of metadata.entries()) {
            const [index, entry] = compactSummaryCatalogEntry(
                value,
                `Compact profiling summary.entries[${metadataIndex}]`,
            );
            if (activeEntries.has(index)) {
                throw new TypeError(
                    `Compact profiling summary.entries[${metadataIndex}][0] is duplicated.`,
                );
            }
            activeEntries.set(index, entry);
        }
    }

    const systems: ProfileEntry[] = [];
    const zones: ProfileEntry[] = [];
    for (const [valueIndex, value] of arrayValue(
        summary.values,
        "Compact profiling summary.values",
    ).entries()) {
        const context = `Compact profiling summary.values[${valueIndex}]`;
        const fields = arrayValue(value, context);
        if (fields.length !== 6) throw new TypeError(`${context} must contain 6 fields.`);
        const entryIndex = nonNegativeIntegerValue(fields[0], `${context}[0]`);
        const metadataEntry = activeEntries.get(entryIndex);
        if (!metadataEntry) {
            throw new TypeError(`${context}[0] does not reference a catalog entry.`);
        }
        const count = numberValue(fields[1], `${context}[1]`);
        const totalMs = numberValue(fields[2], `${context}[2]`);
        const selfMs = numberValue(fields[3], `${context}[3]`);
        const entry: ProfileEntry = {
            ...metadataEntry.entry,
            count,
            totalMs,
            selfMs,
            meanMs: count > 0 ? totalMs / count : 0,
            selfMeanMs: count > 0 ? selfMs / count : 0,
            minMs: numberValue(fields[4], `${context}[4]`),
            maxMs: numberValue(fields[5], `${context}[5]`),
        };
        (metadataEntry.system ? systems : zones).push(entry);
    }

    const frameStats = arrayValue(summary.frame_stats, "Compact profiling summary.frame_stats");
    if (frameStats.length !== 4) {
        throw new TypeError("Compact profiling summary.frame_stats must contain 4 fields.");
    }
    const frameCount = nonNegativeIntegerValue(
        frameStats[0],
        "Compact profiling summary.frame_stats[0]",
    );
    const byTotal = (left: ProfileEntry, right: ProfileEntry) =>
        right.totalMs - left.totalMs || left.name.localeCompare(right.name);
    systems.sort(byTotal);
    zones.sort(byTotal);

    catalog.revision = revision;
    catalog.entries = activeEntries;
    return {
        available: booleanValue(summary.available, "Compact profiling summary.available"),
        frameStats: {
            available: frameCount > 0,
            frameCount,
            fps: numberValue(frameStats[1], "Compact profiling summary.frame_stats[1]"),
            latestFrameMs: numberValue(
                frameStats[2],
                "Compact profiling summary.frame_stats[2]",
            ),
            averageFrameMs: numberValue(
                frameStats[3],
                "Compact profiling summary.frame_stats[3]",
            ),
        },
        systems,
        zones,
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

interface ProfileArchiveEntry {
    system: boolean;
    entry: ProfileEntry;
}

function profileArchiveEntry(value: unknown, context: string): ProfileArchiveEntry {
    const fields = arrayValue(value, context);
    if (fields.length !== 11) throw new TypeError(`${context} must contain 11 fields.`);
    const kind = numberValue(fields[0], `${context}[0]`);
    if (kind !== 0 && kind !== 1) throw new TypeError(`${context}[0] must be 0 or 1.`);
    const symbolKind = stringValue(fields[4], `${context}[4]`);
    if (
        symbolKind !== "none" &&
        symbolKind !== "pe-rva" &&
        symbolKind !== "wasm-function-index"
    ) {
        throw new TypeError(`${context}[4] is not supported.`);
    }
    return {
        system: kind === 1,
        entry: {
            scheduleId: numberValue(fields[1], `${context}[1]`),
            systemId: numberValue(fields[2], `${context}[2]`),
            scheduleName: stringValue(fields[3], `${context}[3]`),
            symbol:
                symbolKind === "none"
                    ? null
                    : {
                          kind: symbolKind,
                          moduleId: stringValue(fields[5], `${context}[5]`),
                          id: numberValue(fields[6], `${context}[6]`),
                      },
            name: stringValue(fields[7], `${context}[7]`),
            file: stringValue(fields[8], `${context}[8]`),
            functionName: stringValue(fields[9], `${context}[9]`),
            line: numberValue(fields[10], `${context}[10]`),
            count: 0,
            totalMs: 0,
            selfMs: 0,
            meanMs: 0,
            selfMeanMs: 0,
            minMs: 0,
            maxMs: 0,
        },
    };
}

export function parseProfileFrameArchive(value: unknown): ProfileFrameDetails {
    const response = objectValue(value, "Profiling frame archive");
    const entries = arrayValue(response.entries, "Profiling frame archive.entries").map(
        (entry, index) => profileArchiveEntry(entry, `Profiling frame archive.entries[${index}]`),
    );
    return {
        available: booleanValue(response.available, "Profiling frame archive.available"),
        details: arrayValue(response.frames, "Profiling frame archive.frames").map(
            (value, frameIndex) => {
                const frame = arrayValue(value, `Profiling frame archive.frames[${frameIndex}]`);
                if (frame.length !== 3) {
                    throw new TypeError(
                        `Profiling frame archive.frames[${frameIndex}] must contain 3 fields.`,
                    );
                }
                const systems: ProfileEntry[] = [];
                const zones: ProfileEntry[] = [];
                for (const [recordIndex, value] of arrayValue(
                    frame[2],
                    `Profiling frame archive.frames[${frameIndex}][2]`,
                ).entries()) {
                    const context = `Profiling frame archive.frames[${frameIndex}][2][${recordIndex}]`;
                    const fields = arrayValue(value, context);
                    if (fields.length !== 6) {
                        throw new TypeError(`${context} must contain 6 fields.`);
                    }
                    const entryIndex = numberValue(fields[0], `${context}[0]`);
                    const metadata = entries[entryIndex];
                    if (!Number.isInteger(entryIndex) || !metadata) {
                        throw new TypeError(`${context}[0] does not reference an archive entry.`);
                    }
                    const count = numberValue(fields[1], `${context}[1]`);
                    const totalMs = numberValue(fields[2], `${context}[2]`);
                    const selfMs = numberValue(fields[3], `${context}[3]`);
                    const entry: ProfileEntry = {
                        ...metadata.entry,
                        count,
                        totalMs,
                        selfMs,
                        meanMs: count > 0 ? totalMs / count : 0,
                        selfMeanMs: count > 0 ? selfMs / count : 0,
                        minMs: numberValue(fields[4], `${context}[4]`),
                        maxMs: numberValue(fields[5], `${context}[5]`),
                    };
                    (metadata.system ? systems : zones).push(entry);
                }
                const byTotal = (left: ProfileEntry, right: ProfileEntry) =>
                    right.totalMs - left.totalMs || left.name.localeCompare(right.name);
                systems.sort(byTotal);
                zones.sort(byTotal);
                return {
                    frame: numberValue(frame[0], `Profiling frame archive.frames[${frameIndex}][0]`),
                    durationMs: numberValue(
                        frame[1],
                        `Profiling frame archive.frames[${frameIndex}][1]`,
                    ),
                    systems,
                    zones,
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

export async function inspectCompactProfileSummary(
    inspect: RuntimeInspect,
    catalog: ProfileSummaryCatalog,
): Promise<ProfileSummary> {
    return parseCompactProfileSummary(
        await inspect(...providers.summaryCompact, { catalog_revision: catalog.revision }),
        catalog,
    );
}

export async function inspectProfileFrameHistory(
    inspect: RuntimeInspect,
    afterFrame: number | null = null,
): Promise<ProfileFrameHistory> {
    return parseProfileFrameHistory(
        await inspect(
            ...providers.frames,
            afterFrame === null ? {} : { after_frame: afterFrame },
        ),
    );
}

export async function inspectProfileFrameDetails(
    inspect: RuntimeInspect,
    frames: number[],
): Promise<ProfileFrameDetails> {
    return parseProfileFrameDetails(await inspect(...providers.frameDetails, { frames }));
}

export async function inspectProfileFrameArchive(
    inspect: RuntimeInspect,
    frames: number[],
): Promise<ProfileFrameDetails> {
    return parseProfileFrameArchive(await inspect(...providers.frameArchive, { frames }));
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
