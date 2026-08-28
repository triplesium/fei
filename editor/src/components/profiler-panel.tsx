import {
    Activity,
    ChevronLeft,
    ChevronRight,
    Circle,
    RefreshCw,
    Search,
    SkipForward,
    Square,
    Timer,
    Trash2,
} from "lucide-react";
import { memo, useCallback, useEffect, useMemo, useRef, useState } from "react";
import { PanelEmptyState, PanelStatus, PanelToolbar, ToolPanel } from "@/components/panel";
import { Button } from "@/components/ui/button";
import { NativeSelect } from "@/components/ui/native-select";
import type { RuntimeState } from "@/runtime/types";
import type {
    ProfileCaptureArchive,
    ProfileCaptureProgress,
} from "@/runtime/profile-capture-archive";
import {
    resolveProfileFrameDetails,
    resolveProfileSummary,
} from "@/services/profile-symbols";
import {
    controlProfiling,
    createProfileSummaryCatalog,
    inspectCompactProfileSummary,
    inspectGpuProfileSummary,
    inspectProfileFrameDetails,
    inspectProfileFrameHistory,
    mergeProfileFrameHistory,
    profileFrameDetailsToRequest,
    startsNewProfileSession,
    type GpuProfileEntry,
    type GpuProfileSummary,
    type ProfileCaptureStatus,
    type ProfileControlAction,
    type ProfileEntry,
    type ProfileFrameDetail,
    type ProfileFrameHistory,
    type ProfileFrameSample,
    type ProfileSummary,
    type RuntimeInspect,
} from "@/runtime/profiling";
import { cn } from "@/lib/utils";

interface ProfilerPanelProps {
    runtimeState: RuntimeState;
    sessionId: string | null;
    inspect: RuntimeInspect;
    archive: ProfileCaptureArchive | null;
    finalizing: boolean;
    finalizeProgress: ProfileCaptureProgress | null;
    onClearArchive(): void;
}

type ProfilerTab = "systems" | "zones" | "gpu";
type CpuSortKey = "name" | "totalMs" | "selfMs" | "count" | "meanMs" | "maxMs";
type GpuSortKey = "name" | "latestMs" | "totalMs" | "count" | "meanMs" | "maxMs";
type SortDirection = "asc" | "desc";

const pollMilliseconds = 500;
const aggregatePollInterval = 2;
const captureFrameOptions = [120, 300, 600] as const;
const countFormatter = new Intl.NumberFormat(undefined, { maximumFractionDigits: 0 });

function percentile(values: readonly number[], fraction: number): number {
    if (values.length === 0) return Number.NaN;
    const sorted = [...values].sort((left, right) => left - right);
    return sorted[
        Math.min(sorted.length - 1, Math.max(0, Math.ceil(sorted.length * fraction) - 1))
    ]!;
}

function errorMessage(error: unknown): string {
    return error instanceof Error ? error.message : String(error);
}

function formatMilliseconds(value: number): string {
    if (!Number.isFinite(value)) return "—";
    if (value > 0 && value < 0.01) return "<0.01 ms";
    return `${value.toFixed(2)} ms`;
}

function formatCount(value: number): string {
    return countFormatter.format(value);
}

function compactProfileName(value: string): string {
    let depth = 0;
    let withoutTemplates = "";
    for (const character of value) {
        if (character === "<") {
            depth += 1;
        } else if (character === ">" && depth > 0) {
            depth -= 1;
        } else if (depth === 0) {
            withoutTemplates += character;
        }
    }

    const parameters = withoutTemplates.lastIndexOf("(");
    let signature =
        parameters >= 0 && withoutTemplates.endsWith(")")
            ? withoutTemplates.slice(0, parameters)
            : withoutTemplates;
    signature = signature.replaceAll("(anonymous namespace)::", "").trim();
    const firstScope = signature.indexOf("::");
    const prefixEnd =
        signature.startsWith("project://")
            ? -1
            : firstScope >= 0
              ? signature.lastIndexOf(" ", firstScope)
              : parameters >= 0
                ? signature.lastIndexOf(" ")
                : -1;
    if (prefixEnd >= 0) signature = signature.slice(prefixEnd + 1);
    return signature || value;
}

function profileSource(entry: ProfileEntry): string | null {
    const file = entry.file.trim();
    if (file && file !== "<unknown>" && entry.line > 0) {
        return `${file}:${entry.line}`;
    }

    const functionName = entry.functionName.trim();
    return functionName && functionName !== "<unknown>"
        ? compactProfileName(functionName)
        : null;
}

function profileTooltip(entry: ProfileEntry): string {
    return [
        entry.scheduleName || null,
        profileSource(entry),
        `${formatCount(entry.count)} calls · Total ${formatMilliseconds(entry.totalMs)} · Mean ${formatMilliseconds(entry.meanMs)} · Min ${formatMilliseconds(entry.minMs)} · Max ${formatMilliseconds(entry.maxMs)}`,
    ]
        .filter((line): line is string => line !== null)
        .join("\n");
}

const FrameChart = memo(function FrameChart({
    frames,
    targetFps,
    selectedFrame,
    onSelect,
    onLive,
}: {
    frames: ProfileFrameSample[];
    targetFps: number;
    selectedFrame: number | null;
    onSelect(frame: number): void;
    onLive(): void;
}) {
    const canvasRef = useRef<HTMLCanvasElement>(null);
    const [hoveredFrame, setHoveredFrame] = useState<ProfileFrameSample | null>(null);
    const [viewRange, setViewRange] = useState<{ start: number; end: number } | null>(null);
    const [frameInput, setFrameInput] = useState(selectedFrame === null ? "" : String(selectedFrame));
    const dragRef = useRef<
        | { mode: "select" }
        | { mode: "pan"; startX: number; range: { start: number; end: number } }
        | null
    >(null);
    const budgetMs = 1_000 / targetFps;
    const range = useMemo(() => {
        if (!viewRange || frames.length === 0) return { start: 0, end: frames.length };
        const width = Math.min(frames.length, Math.max(1, viewRange.end - viewRange.start));
        const start = Math.max(0, Math.min(frames.length - width, viewRange.start));
        return { start, end: start + width };
    }, [frames.length, viewRange]);
    const visibleFrames = useMemo(
        () => frames.slice(range.start, range.end),
        [frames, range.end, range.start],
    );
    const maximumMs = Math.max(
        budgetMs * 1.2,
        ...visibleFrames.map((frame) => frame.durationMs * 1.2),
        1,
    );
    const budgetBottom = Math.min(100, (budgetMs / maximumMs) * 100);
    const p95Ms = percentile(frames.map((frame) => frame.durationMs), 0.95);
    const slowFrames = frames.filter((frame) => frame.durationMs > budgetMs).length;
    const selectedIndex = frames.findIndex((frame) => frame.frame === selectedFrame);
    const zoomed = range.start > 0 || range.end < frames.length;

    useEffect(() => {
        setFrameInput(selectedFrame === null ? "" : String(selectedFrame));
    }, [selectedFrame]);

    useEffect(() => {
        if (frames.length === 0) setViewRange(null);
    }, [frames.length]);

    useEffect(() => {
        const canvas = canvasRef.current;
        if (!canvas) return;

        const draw = (): void => {
            const bounds = canvas.getBoundingClientRect();
            const width = Math.max(1, Math.round(bounds.width));
            const height = Math.max(1, Math.round(bounds.height));
            const scale = Math.max(1, globalThis.devicePixelRatio || 1);
            const pixelWidth = Math.round(width * scale);
            const pixelHeight = Math.round(height * scale);
            if (canvas.width !== pixelWidth || canvas.height !== pixelHeight) {
                canvas.width = pixelWidth;
                canvas.height = pixelHeight;
            }

            const context = canvas.getContext("2d");
            if (!context) return;
            context.setTransform(scale, 0, 0, scale, 0, 0);
            context.clearRect(0, 0, width, height);
            for (let index = 1; index < 4; index += 1) {
                const y = (height * index) / 4;
                context.strokeStyle = "#252a2f";
                context.lineWidth = 1;
                context.beginPath();
                context.moveTo(0, y + 0.5);
                context.lineTo(width, y + 0.5);
                context.stroke();
            }

            if (visibleFrames.length === 0) return;
            const xAt = (index: number): number =>
                visibleFrames.length === 1 ? width / 2 : (index / (visibleFrames.length - 1)) * width;
            const yAt = (durationMs: number): number =>
                height - Math.min(1, Math.max(0, durationMs / maximumMs)) * height;

            context.beginPath();
            context.moveTo(xAt(0), height);
            visibleFrames.forEach((frame, index) => context.lineTo(xAt(index), yAt(frame.durationMs)));
            context.lineTo(xAt(visibleFrames.length - 1), height);
            context.closePath();
            const fill = context.createLinearGradient(0, 0, 0, height);
            fill.addColorStop(0, "rgba(74, 168, 255, 0.28)");
            fill.addColorStop(1, "rgba(74, 168, 255, 0.025)");
            context.fillStyle = fill;
            context.fill();

            context.beginPath();
            visibleFrames.forEach((frame, index) => {
                const x = xAt(index);
                const y = yAt(frame.durationMs);
                if (index === 0) context.moveTo(x, y);
                else context.lineTo(x, y);
            });
            context.strokeStyle = "#58adff";
            context.lineWidth = 1.5;
            context.stroke();

            visibleFrames.forEach((frame, index) => {
                if (frame.durationMs <= budgetMs) return;
                context.fillStyle = frame.durationMs > budgetMs * 1.5 ? "#fb7185" : "#fbbf24";
                context.beginPath();
                context.arc(xAt(index), yAt(frame.durationMs), 2.25, 0, Math.PI * 2);
                context.fill();
            });

            const drawCursor = (frame: ProfileFrameSample | null, color: string, lineWidth: number): void => {
                if (!frame) return;
                const index = visibleFrames.findIndex((candidate) => candidate.frame === frame.frame);
                if (index < 0) return;
                const x = Math.round(xAt(index)) + 0.5;
                context.strokeStyle = color;
                context.lineWidth = lineWidth;
                context.beginPath();
                context.moveTo(x, 0);
                context.lineTo(x, height);
                context.stroke();
            };
            drawCursor(hoveredFrame, "rgba(219, 231, 242, 0.38)", 1);
            drawCursor(
                visibleFrames.find((frame) => frame.frame === selectedFrame) ?? null,
                "rgba(255, 255, 255, 0.9)",
                1.5,
            );
        };

        draw();
        const observer = new ResizeObserver(draw);
        observer.observe(canvas);
        return () => observer.disconnect();
    }, [budgetMs, hoveredFrame, maximumMs, selectedFrame, visibleFrames]);

    const frameAt = (clientX: number, canvas: HTMLCanvasElement): ProfileFrameSample | null => {
        if (visibleFrames.length === 0) return null;
        const bounds = canvas.getBoundingClientRect();
        if (bounds.width <= 0) return null;
        const ratio = Math.max(0, Math.min(0.999_999, (clientX - bounds.left) / bounds.width));
        return visibleFrames[Math.round(ratio * (visibleFrames.length - 1))] ?? null;
    };

    const commitFrameInput = (): void => {
        const requested = Number(frameInput);
        if (!Number.isFinite(requested) || frames.length === 0) {
            setFrameInput(selectedFrame === null ? "" : String(selectedFrame));
            return;
        }
        const nearest = frames.reduce((best, frame) =>
            Math.abs(frame.frame - requested) < Math.abs(best.frame - requested) ? frame : best,
        );
        onSelect(nearest.frame);
    };

    return (
        <section className="relative min-h-[180px] flex-1 overflow-hidden bg-[#181a1d] px-3 pt-10 pb-5">
            <header className="absolute inset-x-3 top-1.5 z-20 flex h-7 items-center gap-2 text-[10px]">
                <strong className="whitespace-nowrap text-[#cbd2da]">Frame time</strong>
                <div className="ml-auto flex shrink-0 items-center gap-0.5">
                    <Button variant="ghost" size="icon" className="size-6" aria-label="Select previous profile frame" disabled={selectedIndex <= 0} onClick={() => onSelect(frames[selectedIndex - 1]?.frame ?? frames[0]!.frame)}>
                        <ChevronLeft size={12} />
                    </Button>
                    <input
                        aria-label="Profile frame number"
                        inputMode="numeric"
                        className="h-6 w-[72px] rounded border border-[#3d444c] bg-[#141619] px-1.5 text-right font-mono text-[10px] outline-none focus:border-primary/70"
                        value={frameInput}
                        placeholder="Frame #"
                        onChange={(event) => setFrameInput(event.target.value)}
                        onBlur={commitFrameInput}
                        onKeyDown={(event) => {
                            if (event.key === "Enter") {
                                commitFrameInput();
                                event.currentTarget.blur();
                            }
                        }}
                    />
                    <Button variant="ghost" size="icon" className="size-6" aria-label="Select next profile frame" disabled={selectedIndex < 0 || selectedIndex >= frames.length - 1} onClick={() => onSelect(frames[selectedIndex + 1]?.frame ?? frames.at(-1)!.frame)}>
                        <ChevronRight size={12} />
                    </Button>
                    <Button variant="ghost" size="sm" className="h-6 px-1.5 text-[10px]" disabled={frames.length === 0} onClick={onLive}>
                        <SkipForward size={11} /> Live
                    </Button>
                    {zoomed && (
                        <button type="button" className="h-6 rounded px-1.5 font-mono text-[9px] text-[#91a6b8] hover:bg-white/5 hover:text-foreground" onClick={() => setViewRange(null)}>
                            {visibleFrames.length} / {frames.length} · Reset
                        </button>
                    )}
                </div>
            </header>
            <div className="absolute inset-x-3 top-10 bottom-5">
                <span className="pointer-events-none absolute top-0 left-0 z-10 max-w-[58%] truncate bg-[#181a1d]/85 pr-1 font-mono text-[9px] text-[#66717d]">
                    {hoveredFrame
                        ? `Frame ${hoveredFrame.frame} · ${formatMilliseconds(hoveredFrame.durationMs)}`
                        : `Max ${formatMilliseconds(maximumMs)} · P95 ${formatMilliseconds(p95Ms)} · ${slowFrames} over`}
                </span>
                <div
                    className="pointer-events-none absolute right-0 left-0 z-10 border-t border-dashed border-[#fbbf24]/55"
                    style={{ bottom: `${budgetBottom}%` }}
                >
                    <span className="absolute right-0 -top-3.5 bg-[#181a1d] pl-1 font-mono text-[9px] text-[#cda92e]">
                        {targetFps} FPS · {budgetMs.toFixed(2)} ms
                    </span>
                </div>
                <canvas
                    ref={canvasRef}
                    className="size-full cursor-crosshair touch-none outline-none focus-visible:ring-1 focus-visible:ring-white"
                    aria-label={`Captured frame times, ${frames.length} frames`}
                    role="img"
                    tabIndex={0}
                    onContextMenu={(event) => event.preventDefault()}
                    onPointerDown={(event) => {
                        if (event.button === 0) {
                            dragRef.current = { mode: "select" };
                            const frame = frameAt(event.clientX, event.currentTarget);
                            if (frame) onSelect(frame.frame);
                        } else if (event.button === 1 || event.button === 2) {
                            dragRef.current = { mode: "pan", startX: event.clientX, range };
                        } else {
                            return;
                        }
                        event.currentTarget.setPointerCapture(event.pointerId);
                    }}
                    onPointerMove={(event) => {
                        const frame = frameAt(event.clientX, event.currentTarget);
                        setHoveredFrame(frame);
                        const drag = dragRef.current;
                        if (drag?.mode === "select" && frame) onSelect(frame.frame);
                        if (drag?.mode === "pan") {
                            const bounds = event.currentTarget.getBoundingClientRect();
                            if (bounds.width <= 0) return;
                            const width = drag.range.end - drag.range.start;
                            const delta = Math.round(((drag.startX - event.clientX) / bounds.width) * width);
                            const start = Math.max(0, Math.min(frames.length - width, drag.range.start + delta));
                            setViewRange({ start, end: start + width });
                        }
                    }}
                    onPointerLeave={() => setHoveredFrame(null)}
                    onPointerUp={(event) => {
                        dragRef.current = null;
                        if (event.currentTarget.hasPointerCapture(event.pointerId)) event.currentTarget.releasePointerCapture(event.pointerId);
                    }}
                    onPointerCancel={() => {
                        dragRef.current = null;
                    }}
                    onWheel={(event) => {
                        if (frames.length < 2) return;
                        event.preventDefault();
                        const bounds = event.currentTarget.getBoundingClientRect();
                        const anchor = Math.max(0, Math.min(1, (event.clientX - bounds.left) / bounds.width));
                        const currentWidth = range.end - range.start;
                        const nextWidth = Math.max(12, Math.min(frames.length, Math.round(currentWidth * (event.deltaY < 0 ? 0.8 : 1.25))));
                        if (nextWidth >= frames.length) {
                            setViewRange(null);
                            return;
                        }
                        const anchorIndex = range.start + anchor * Math.max(0, currentWidth - 1);
                        const start = Math.max(0, Math.min(frames.length - nextWidth, Math.round(anchorIndex - anchor * (nextWidth - 1))));
                        setViewRange({ start, end: start + nextWidth });
                    }}
                    onDoubleClick={() => setViewRange(null)}
                    onKeyDown={(event) => {
                        if (event.key === "Escape") {
                            onLive();
                            return;
                        }
                        if (
                            frames.length === 0 ||
                            (event.key !== "ArrowLeft" && event.key !== "ArrowRight")
                        ) {
                            return;
                        }
                        event.preventDefault();
                        const currentIndex = Math.max(
                            0,
                            frames.findIndex((frame) => frame.frame === selectedFrame),
                        );
                        const offset = event.key === "ArrowLeft" ? -1 : 1;
                        const next =
                            frames[
                                Math.max(
                                    0,
                                    Math.min(frames.length - 1, currentIndex + offset),
                                )
                            ];
                        if (next) onSelect(next.frame);
                    }}
                />
            </div>
            <span className="absolute bottom-0.5 left-3 font-mono text-[9px] text-[#606a75]">
                {visibleFrames.length > 0 ? `Frame ${visibleFrames[0]?.frame}` : "No frames"}
            </span>
            <span className="absolute right-3 bottom-0.5 font-mono text-[9px] text-[#606a75]">
                {visibleFrames.length > 0 ? `Frame ${visibleFrames.at(-1)?.frame}` : ""}
            </span>
        </section>
    );
});

function SortButton({
    label,
    active,
    direction,
    onClick,
}: {
    label: string;
    active: boolean;
    direction: SortDirection;
    onClick(): void;
}) {
    return (
        <button
            type="button"
            className={cn(
                "w-full whitespace-nowrap border-0 bg-transparent px-2 py-1.5 text-left text-[10px] font-bold tracking-[0.06em] text-[#7f8791] uppercase hover:text-foreground",
                active && "text-[#a9cdef]",
            )}
            onClick={onClick}
        >
            {label}{active ? (direction === "desc" ? " ↓" : " ↑") : ""}
        </button>
    );
}

function CpuEntriesTable({ entries, filter }: { entries: ProfileEntry[]; filter: string }) {
    const [sortKey, setSortKey] = useState<CpuSortKey>("selfMs");
    const [direction, setDirection] = useState<SortDirection>("desc");
    const normalizedFilter = filter.trim().toLocaleLowerCase();
    const rows = useMemo(() => {
        const filtered = normalizedFilter
            ? entries.filter((entry) =>
                  [entry.name, entry.scheduleName, entry.file, entry.functionName].some((value) =>
                      value.toLocaleLowerCase().includes(normalizedFilter),
                  ),
              )
            : entries;
        return [...filtered].sort((left, right) => {
            const leftValue = left[sortKey];
            const rightValue = right[sortKey];
            const result =
                typeof leftValue === "string"
                    ? leftValue.localeCompare(String(rightValue))
                    : leftValue - Number(rightValue);
            return direction === "asc" ? result : -result;
        });
    }, [direction, entries, normalizedFilter, sortKey]);

    const toggleSort = (key: CpuSortKey): void => {
        if (sortKey === key) setDirection((current) => (current === "desc" ? "asc" : "desc"));
        else {
            setSortKey(key);
            setDirection(key === "name" ? "asc" : "desc");
        }
    };

    if (rows.length === 0) {
        return <PanelEmptyState>{normalizedFilter ? "No matching samples." : "No CPU samples captured."}</PanelEmptyState>;
    }

    return (
        <div className="min-h-0 flex-1 overflow-auto bg-[#1b1b1b]">
            <table className="w-full table-fixed border-collapse font-mono text-[11px]">
                <thead className="sticky top-0 z-10 bg-[#242424]">
                    <tr className="border-b border-[#363636]">
                        <th className="w-[68%]"><SortButton label="Name" active={sortKey === "name"} direction={direction} onClick={() => toggleSort("name")} /></th>
                        <th><SortButton label="Self" active={sortKey === "selfMs"} direction={direction} onClick={() => toggleSort("selfMs")} /></th>
                    </tr>
                </thead>
                <tbody>
                    {rows.map((entry) => (
                        <tr
                            key={`${entry.scheduleId}:${entry.systemId}:${entry.file}:${entry.line}:${entry.name}`}
                            className="border-b border-[#292929] text-[#b8c0ca] hover:bg-[#252c33]"
                            title={profileTooltip(entry)}
                        >
                            <td className="px-2 py-1.5">
                                <div className="truncate text-[#d0d5db]">
                                    {compactProfileName(entry.name)}
                                </div>
                            </td>
                            <td className="whitespace-nowrap px-2 py-1.5 text-[#8fc8ff]">{formatMilliseconds(entry.selfMs)}</td>
                        </tr>
                    ))}
                </tbody>
            </table>
        </div>
    );
}

function GpuEntriesTable({ entries, filter }: { entries: GpuProfileEntry[]; filter: string }) {
    const [sortKey, setSortKey] = useState<GpuSortKey>("latestMs");
    const [direction, setDirection] = useState<SortDirection>("desc");
    const normalizedFilter = filter.trim().toLocaleLowerCase();
    const rows = useMemo(() => {
        const filtered = normalizedFilter
            ? entries.filter((entry) => entry.name.toLocaleLowerCase().includes(normalizedFilter))
            : entries;
        return [...filtered].sort((left, right) => {
            const leftValue = left[sortKey];
            const rightValue = right[sortKey];
            const result =
                typeof leftValue === "string"
                    ? leftValue.localeCompare(String(rightValue))
                    : leftValue - Number(rightValue);
            return direction === "asc" ? result : -result;
        });
    }, [direction, entries, normalizedFilter, sortKey]);

    const toggleSort = (key: GpuSortKey): void => {
        if (sortKey === key) setDirection((current) => (current === "desc" ? "asc" : "desc"));
        else {
            setSortKey(key);
            setDirection(key === "name" ? "asc" : "desc");
        }
    };

    if (rows.length === 0) {
        return <PanelEmptyState>{normalizedFilter ? "No matching GPU samples." : "No GPU samples captured."}</PanelEmptyState>;
    }

    return (
        <div className="min-h-0 flex-1 overflow-auto bg-[#1b1b1b]">
            <table className="w-full table-fixed border-collapse font-mono text-[11px]">
                <thead className="sticky top-0 z-10 bg-[#242424]">
                    <tr className="border-b border-[#363636]">
                        <th className="w-1/2"><SortButton label="Name" active={sortKey === "name"} direction={direction} onClick={() => toggleSort("name")} /></th>
                        <th><SortButton label="Latest" active={sortKey === "latestMs"} direction={direction} onClick={() => toggleSort("latestMs")} /></th>
                        <th><SortButton label="Max" active={sortKey === "maxMs"} direction={direction} onClick={() => toggleSort("maxMs")} /></th>
                    </tr>
                </thead>
                <tbody>
                    {rows.map((entry) => (
                        <tr
                            key={entry.name}
                            className="border-b border-[#292929] text-[#b8c0ca] hover:bg-[#252c33]"
                            title={`Total ${formatMilliseconds(entry.totalMs)} · Mean ${formatMilliseconds(entry.meanMs)} · Min ${formatMilliseconds(entry.minMs)} · ${formatCount(entry.count)} calls`}
                        >
                            <td className="truncate px-2 py-1.5 text-[#d0d5db]">{entry.name}</td>
                            <td className="whitespace-nowrap px-2 py-1.5 text-[#bfa7ff]">{formatMilliseconds(entry.latestMs)}</td>
                            <td className="whitespace-nowrap px-2 py-1.5">{formatMilliseconds(entry.maxMs)}</td>
                        </tr>
                    ))}
                </tbody>
            </table>
        </div>
    );
}

function captureLabel(status: ProfileCaptureStatus | null): string {
    if (!status) return "Waiting for profiler";
    if (!status.available) return "CPU instrumentation unavailable";
    if (!status.recording) return "Capture paused";
    if (!status.bounded) return "Live capture";
    return `Capturing ${formatCount(status.frameLimit - status.framesRemaining)} / ${formatCount(status.frameLimit)}`;
}

export function ProfilerPanel({
    runtimeState,
    sessionId,
    inspect,
    archive,
    finalizing,
    finalizeProgress,
    onClearArchive,
}: ProfilerPanelProps) {
    const [summary, setSummary] = useState<ProfileSummary | null>(null);
    const [history, setHistory] = useState<ProfileFrameHistory | null>(null);
    const [frameDetails, setFrameDetails] = useState<Map<number, ProfileFrameDetail>>(
        () => new Map(),
    );
    const [gpuSummary, setGpuSummary] = useState<GpuProfileSummary | null>(null);
    const [captureStatus, setCaptureStatus] = useState<ProfileCaptureStatus | null>(null);
    const [captureFrames, setCaptureFrames] = useState(300);
    const [targetFps, setTargetFps] = useState(60);
    const [selectedFrame, setSelectedFrame] = useState<number | null>(null);
    const [activeTab, setActiveTab] = useState<ProfilerTab>("systems");
    const [filter, setFilter] = useState("");
    const [controlBusy, setControlBusy] = useState(false);
    const [error, setError] = useState("");
    const [lastUpdated, setLastUpdated] = useState("");
    const [refreshToken, setRefreshToken] = useState(0);
    const retainedSessionId = useRef<string | null>(null);
    const historyRef = useRef<ProfileFrameHistory | null>(null);
    const retainedFrameNumbersRef = useRef<Set<number>>(new Set());
    const frameDetailsRef = useRef<Map<number, ProfileFrameDetail>>(new Map());
    const summaryCatalogRef = useRef(createProfileSummaryCatalog());

    const clearLocalData = useCallback(() => {
        setSummary(null);
        summaryCatalogRef.current = createProfileSummaryCatalog();
        historyRef.current = null;
        retainedFrameNumbersRef.current.clear();
        setHistory(null);
        frameDetailsRef.current.clear();
        setFrameDetails(new Map());
        setGpuSummary(null);
        setCaptureStatus(null);
        setSelectedFrame(null);
        setError("");
        setLastUpdated("");
    }, []);

    useEffect(() => {
        if (!startsNewProfileSession(retainedSessionId.current, sessionId)) return;
        retainedSessionId.current = sessionId;
        clearLocalData();
    }, [clearLocalData, sessionId]);

    useEffect(() => {
        if (!archive) return;
        let cancelled = false;
        retainedSessionId.current = archive.sessionId;
        if (archive.summary) setSummary(archive.summary);
        if (archive.history) {
            historyRef.current = archive.history;
            retainedFrameNumbersRef.current = new Set(
                archive.history.frames.map((frame) => frame.frame),
            );
            setHistory(archive.history);
        }
        const retainedDetails = new Map(frameDetailsRef.current);
        for (const [frame, detail] of archive.details) {
            retainedDetails.set(frame, detail);
        }
        frameDetailsRef.current = retainedDetails;
        setFrameDetails(retainedDetails);
        if (archive.gpuSummary) setGpuSummary(archive.gpuSummary);
        setCaptureStatus((current) =>
            current ? { ...current, recording: false, framesRemaining: 0 } : current,
        );
        setError(archive.warnings[0] ?? "");
        setLastUpdated(
            new Date(archive.finalizedAt).toLocaleTimeString([], {
                hour12: false,
                hour: "2-digit",
                minute: "2-digit",
                second: "2-digit",
            }),
        );
        void Promise.all([
            archive.summary ? resolveProfileSummary(archive.summary) : Promise.resolve(null),
            resolveProfileFrameDetails({
                available: archive.history?.available ?? archive.details.size > 0,
                details: [...archive.details.values()],
            }),
        ]).then(([resolvedSummary, resolvedDetails]) => {
            if (cancelled) return;
            if (resolvedSummary) setSummary(resolvedSummary);
            const resolvedByFrame = new Map(frameDetailsRef.current);
            for (const detail of resolvedDetails.details) {
                resolvedByFrame.set(detail.frame, detail);
            }
            frameDetailsRef.current = resolvedByFrame;
            setFrameDetails(resolvedByFrame);
        });
        return () => {
            cancelled = true;
        };
    }, [archive]);

    useEffect(() => {
        if (runtimeState !== "running" || !sessionId || finalizing) return;
        let cancelled = false;
        let polling = false;
        let pollIteration = 0;

        const poll = async (): Promise<void> => {
            if (polling || document.visibilityState === "hidden") return;
            polling = true;
            try {
                const refreshAggregates = pollIteration % aggregatePollInterval === 0;
                pollIteration += 1;
                const results = await Promise.allSettled([
                    refreshAggregates
                        ? inspectCompactProfileSummary(inspect, summaryCatalogRef.current)
                        : Promise.resolve(null),
                    inspectProfileFrameHistory(
                        inspect,
                        historyRef.current?.frames.at(-1)?.frame ?? null,
                    ),
                    refreshAggregates
                        ? inspectGpuProfileSummary(inspect)
                        : Promise.resolve(null),
                    controlProfiling(inspect, "status"),
                ]);
                if (cancelled) return;

                const [summaryResult, historyResult, gpuResult, statusResult] = results;
                let detailFailure = "";
                let nextHistory = historyRef.current;
                let nextFrameDetails = frameDetailsRef.current;
                if (historyResult.status === "fulfilled") {
                    const previousFrames = historyRef.current?.frames ?? [];
                    const mergedFrames = historyResult.value.available
                        ? mergeProfileFrameHistory(
                              previousFrames,
                              historyResult.value.frames,
                          )
                        : previousFrames.length > 0
                          ? []
                          : previousFrames;
                    const historyFramesChanged = mergedFrames !== previousFrames;
                    if (
                        !nextHistory ||
                        nextHistory.available !== historyResult.value.available ||
                        nextHistory.frames !== mergedFrames
                    ) {
                        nextHistory = {
                            available: historyResult.value.available,
                            frames: mergedFrames,
                        };
                    }
                    if (historyFramesChanged) {
                        retainedFrameNumbersRef.current = new Set(
                            mergedFrames.map((frame) => frame.frame),
                        );
                        const staleDetailFrames = [...nextFrameDetails.keys()].filter(
                            (frame) =>
                                !retainedFrameNumbersRef.current.has(frame) &&
                                frame !== selectedFrame,
                        );
                        if (staleDetailFrames.length > 0) {
                            nextFrameDetails = new Map(nextFrameDetails);
                            staleDetailFrames.forEach((frame) =>
                                nextFrameDetails.delete(frame),
                            );
                        }
                    }
                    const detailFrames = profileFrameDetailsToRequest(
                        selectedFrame,
                        retainedFrameNumbersRef.current,
                        nextFrameDetails,
                    );
                    if (detailFrames.length > 0) {
                        try {
                            const response = await resolveProfileFrameDetails(
                                await inspectProfileFrameDetails(inspect, detailFrames),
                            );
                            nextFrameDetails = new Map(nextFrameDetails);
                            for (const detail of response.details) {
                                nextFrameDetails.set(detail.frame, detail);
                            }
                        } catch (caught) {
                            detailFailure = errorMessage(caught);
                        }
                    }
                }

                const resolvedSummary =
                    summaryResult.status === "fulfilled" && summaryResult.value
                        ? await resolveProfileSummary(summaryResult.value)
                        : null;
                if (cancelled) return;

                if (resolvedSummary) setSummary(resolvedSummary);
                if (historyResult.status === "fulfilled") {
                    if (nextHistory !== historyRef.current) {
                        historyRef.current = nextHistory;
                        setHistory(nextHistory);
                    }
                    if (nextFrameDetails !== frameDetailsRef.current) {
                        frameDetailsRef.current = nextFrameDetails;
                        setFrameDetails(nextFrameDetails);
                    }
                }
                if (gpuResult.status === "fulfilled" && gpuResult.value) {
                    setGpuSummary(gpuResult.value);
                }
                if (statusResult.status === "fulfilled") setCaptureStatus(statusResult.value);

                const failures = results
                    .filter(
                        (result): result is PromiseRejectedResult =>
                            result.status === "rejected",
                    )
                    .map((result) => errorMessage(result.reason));
                setError(failures[0] ?? detailFailure);
                if (results.some((result) => result.status === "fulfilled")) {
                    setLastUpdated(
                        new Date().toLocaleTimeString([], {
                            hour12: false,
                            hour: "2-digit",
                            minute: "2-digit",
                            second: "2-digit",
                        }),
                    );
                }
            } finally {
                polling = false;
            }
        };

        void poll();
        const timer = globalThis.setInterval(() => void poll(), pollMilliseconds);
        return () => {
            cancelled = true;
            globalThis.clearInterval(timer);
        };
    }, [finalizing, inspect, refreshToken, runtimeState, selectedFrame, sessionId]);

    const runControl = async (action: ProfileControlAction): Promise<void> => {
        if (controlBusy || finalizing) return;
        if (action === "clear" && (runtimeState !== "running" || !sessionId)) {
            clearLocalData();
            onClearArchive();
            return;
        }
        if (runtimeState !== "running" || !sessionId) return;
        setControlBusy(true);
        setError("");
        try {
            const status = await controlProfiling(
                inspect,
                action,
                action === "capture" ? captureFrames : undefined,
            );
            if (action === "start" || action === "capture" || action === "clear") {
                clearLocalData();
            }
            if (action === "clear") onClearArchive();
            setCaptureStatus(status);
            setRefreshToken((current) => current + 1);
        } catch (caught) {
            setError(errorMessage(caught));
        } finally {
            setControlBusy(false);
        }
    };

    const frames = history?.frames ?? [];
    const analysisFrame = selectedFrame ?? frames.at(-1)?.frame ?? null;
    const selectedSample = frames.find((frame) => frame.frame === analysisFrame);
    const selectedDetail =
        selectedFrame === null ? undefined : frameDetails.get(selectedFrame);
    const cpuSystems =
        selectedFrame === null ? (summary?.systems ?? []) : (selectedDetail?.systems ?? []);
    const cpuZones =
        selectedFrame === null ? (summary?.zones ?? []) : (selectedDetail?.zones ?? []);
    const dataAvailable = summary !== null || history !== null || gpuSummary !== null;
    const selectedFrameMs = selectedSample?.durationMs ?? selectedDetail?.durationMs ?? Number.NaN;
    const controlsDisabled =
        runtimeState !== "running" || !sessionId || controlBusy || finalizing;
    const tabs: readonly { id: ProfilerTab; label: string; count?: number }[] = [
        { id: "systems", label: "Systems", count: cpuSystems.length },
        { id: "zones", label: "Zones", count: cpuZones.length },
        { id: "gpu", label: "GPU", count: gpuSummary?.entries.length },
    ];

    return (
        <ToolPanel className="bg-[#1b1b1b]">
            <PanelToolbar className="h-auto min-h-[34px] flex-wrap justify-start gap-1 py-1">
                <Button
                    variant={captureStatus?.recording ? "secondary" : "ghost"}
                    size="sm"
                    className={cn("h-6 px-2", captureStatus?.recording && "text-[#8cddb5]")}
                    disabled={controlsDisabled || captureStatus?.available === false}
                    onClick={() => void runControl(captureStatus?.recording ? "stop" : "start")}
                >
                    {captureStatus?.recording ? <Square size={11} fill="currentColor" /> : <Circle size={11} fill="currentColor" />}
                    {captureStatus?.recording ? "Stop" : "Record"}
                </Button>
                <span className="h-4 border-l border-[#4a4a4a]" />
                <NativeSelect
                    aria-label="Capture frame count"
                    className="h-6 w-[76px] border-[#454545] bg-[#222] py-0 pr-6 pl-2 font-mono text-[10px]"
                    value={captureFrames}
                    disabled={controlsDisabled}
                    onChange={(event) => setCaptureFrames(Number(event.target.value))}
                >
                    {captureFrameOptions.map((frames) => <option key={frames} value={frames}>{frames} f</option>)}
                </NativeSelect>
                <Button variant="ghost" size="sm" className="h-6 px-2" disabled={controlsDisabled || captureStatus?.available === false} onClick={() => void runControl("capture")}>
                    <Timer size={12} /> Capture
                </Button>
                <Button variant="ghost" size="icon" className="size-6" aria-label="Clear profiling capture" disabled={controlBusy || finalizing || (!dataAvailable && runtimeState !== "running")} onClick={() => void runControl("clear")}>
                    <Trash2 size={12} />
                </Button>
                <Button variant="ghost" size="icon" className="size-6" aria-label="Refresh profiler" disabled={runtimeState !== "running" || !sessionId || finalizing} onClick={() => setRefreshToken((current) => current + 1)}>
                    <RefreshCw size={12} className={controlBusy ? "animate-spin" : ""} />
                </Button>
                <span className="ml-auto text-[10px]">Target</span>
                <NativeSelect
                    aria-label="Target frame rate"
                    className="h-6 w-[74px] border-[#454545] bg-[#222] py-0 pr-6 pl-2 font-mono text-[10px]"
                    value={targetFps}
                    onChange={(event) => setTargetFps(Number(event.target.value))}
                >
                    <option value={30}>30 FPS</option>
                    <option value={60}>60 FPS</option>
                    <option value={120}>120 FPS</option>
                </NativeSelect>
            </PanelToolbar>

            {!dataAvailable && runtimeState !== "running" ? (
                <PanelEmptyState>
                    <Activity size={28} strokeWidth={1.3} />
                    <div className="grid gap-1">
                        <strong className="text-xs text-[#b7bdc8]">No profiling session</strong>
                        <span>Start the runtime, record a capture, then select a frame spike to inspect it.</span>
                    </div>
                </PanelEmptyState>
            ) : (
                <div className="grid min-h-0 flex-1 grid-cols-[minmax(0,0.85fr)_minmax(0,1.15fr)] grid-rows-[minmax(0,1fr)] overflow-hidden">
                    <section className="flex min-w-0 flex-col border-r border-[#343434] bg-[#1b1b1b]">
                        <div className="flex h-[31px] shrink-0 items-center border-b border-[#343434] bg-[#222]">
                            {tabs.map((tab) => (
                                <button
                                    key={tab.id}
                                    type="button"
                                    className={cn(
                                        "h-full border-0 border-b-2 border-transparent bg-transparent px-2.5 text-[10px] font-semibold text-muted-foreground hover:bg-white/5 hover:text-foreground",
                                        activeTab === tab.id && "border-primary text-foreground",
                                    )}
                                    onClick={() => setActiveTab(tab.id)}
                                >
                                    {tab.label}
                                    {tab.count !== undefined && <span className="ml-1 text-[#5f6975]">{tab.count}</span>}
                                </button>
                            ))}
                            <label className="relative ml-auto mr-1.5 block min-w-16 flex-1 max-w-[140px]">
                                <Search className="pointer-events-none absolute top-1/2 left-2 size-3 -translate-y-1/2 text-[#66707b]" />
                                <input
                                    type="search"
                                    className="h-6 w-full rounded border border-[#3c3c3c] bg-[#191919] pr-2 pl-6 text-[10px] outline-none placeholder:text-[#606060] focus:border-primary/60"
                                    value={filter}
                                    placeholder="Filter"
                                    onChange={(event) => setFilter(event.target.value)}
                                />
                            </label>
                        </div>
                        <div className="flex h-7 shrink-0 items-center gap-2 border-b border-[#303030] bg-[#1e2023] px-2.5 text-[10px]">
                            <span className="font-medium text-[#c4ccd5]">
                                {analysisFrame === null
                                    ? "No frame selected"
                                    : selectedFrame === null
                                      ? `Live · Frame ${analysisFrame}`
                                      : `Frame ${analysisFrame}`}
                            </span>
                            <span className="font-mono text-[#8fc8ff]">
                                {formatMilliseconds(selectedFrameMs)}
                            </span>
                            {selectedFrame !== null && !selectedDetail && summary?.available !== false && (
                                <span className="ml-auto truncate text-[#8aa9c1]">
                                    {runtimeState === "running" ? "Loading details…" : "Details not retained"}
                                </span>
                            )}
                        </div>
                        {summary?.available === false && (
                            <div className="shrink-0 border-b border-[#705b20] bg-[#3a3015]/45 px-2.5 py-1.5 text-[10px] text-[#d8bd62]">
                                CPU profiling requires <code className="font-mono">--profile_summary=y</code>.
                            </div>
                        )}
                        {activeTab === "systems" && <CpuEntriesTable entries={cpuSystems} filter={filter} />}
                        {activeTab === "zones" && <CpuEntriesTable entries={cpuZones} filter={filter} />}
                        {activeTab === "gpu" && (
                            <div className="flex min-h-0 flex-1 flex-col">
                                <div className="shrink-0 border-b border-[#343434] bg-[#202020] px-2.5 py-1.5 text-[10px] text-[#78838f]">
                                    Capture-wide GPU timestamps
                                </div>
                                <GpuEntriesTable entries={gpuSummary?.entries ?? []} filter={filter} />
                            </div>
                        )}
                    </section>
                    <section className="flex min-h-0 min-w-0 flex-col overflow-hidden bg-[#181a1d]">
                        <FrameChart
                            frames={frames}
                            targetFps={targetFps}
                            selectedFrame={analysisFrame}
                            onSelect={setSelectedFrame}
                            onLive={() => setSelectedFrame(null)}
                        />
                    </section>
                </div>
            )}

            <PanelStatus>
                <span className={cn("truncate", error && "text-[#ff9aaa]")} title={error || undefined}>
                    {finalizing
                        ? finalizeProgress
                            ? `Finalizing profiler capture… ${finalizeProgress.retained} / ${finalizeProgress.total} frames`
                            : "Finalizing profiler capture…"
                        : error ||
                          (runtimeState === "running"
                              ? captureLabel(captureStatus)
                              : archive
                                ? archive.complete
                                    ? "Runtime stopped · capture retained"
                                    : `Runtime stopped · ${frameDetails.size} / ${archive.history?.frames.length ?? 0} frame details retained`
                                : "Runtime stopped · capture frozen")}
                </span>
                <span className="shrink-0 font-mono">
                    {analysisFrame !== null
                        ? `${selectedFrame === null ? "Live" : "Pinned"} · Frame ${analysisFrame} · ${formatMilliseconds(selectedSample?.durationMs ?? selectedDetail?.durationMs ?? Number.NaN)}`
                        : lastUpdated
                          ? `Updated ${lastUpdated}`
                          : "—"}
                </span>
            </PanelStatus>
        </ToolPanel>
    );
}
