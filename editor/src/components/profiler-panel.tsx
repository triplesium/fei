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
import { useCallback, useEffect, useMemo, useRef, useState } from "react";
import { PanelEmptyState, PanelStatus, PanelToolbar, ToolPanel } from "@/components/panel";
import { Button } from "@/components/ui/button";
import { NativeSelect } from "@/components/ui/native-select";
import type { RuntimeState } from "@/runtime/types";
import {
    resolveProfileFrameDetails,
    resolveProfileSummary,
} from "@/services/profile-symbols";
import {
    controlProfiling,
    inspectGpuProfileSummary,
    inspectProfileFrameDetails,
    inspectProfileFrameHistory,
    inspectProfileSummary,
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
}

type ProfilerTab = "overview" | "systems" | "zones" | "gpu";
type CpuSortKey = "name" | "totalMs" | "selfMs" | "count" | "meanMs" | "maxMs";
type GpuSortKey = "name" | "latestMs" | "totalMs" | "count" | "meanMs" | "maxMs";
type SortDirection = "asc" | "desc";

const pollMilliseconds = 250;
const frameDetailBatchSize = 60;
const captureFrameOptions = [120, 300, 600] as const;
const countFormatter = new Intl.NumberFormat(undefined, { maximumFractionDigits: 0 });

function errorMessage(error: unknown): string {
    return error instanceof Error ? error.message : String(error);
}

function formatMilliseconds(value: number): string {
    if (!Number.isFinite(value)) return "—";
    if (value > 0 && value < 0.01) return `${(value * 1_000).toFixed(1)} µs`;
    if (value < 1) return `${value.toFixed(3)} ms`;
    return `${value.toFixed(2)} ms`;
}

function formatCount(value: number): string {
    return countFormatter.format(value);
}

function FrameChart({
    frames,
    targetFps,
    selectedFrame,
    onSelect,
}: {
    frames: ProfileFrameSample[];
    targetFps: number;
    selectedFrame: number | null;
    onSelect(frame: number): void;
}) {
    const budgetMs = 1_000 / targetFps;
    const maximumMs = Math.max(
        budgetMs * 1.25,
        ...frames.map((frame) => frame.durationMs),
        1,
    );
    const budgetBottom = Math.min(100, (budgetMs / maximumMs) * 100);

    return (
        <div className="relative h-[132px] overflow-hidden border-b border-[#171717] bg-[#191919] px-2.5 pt-5 pb-4">
            <span className="absolute top-1 left-2.5 z-10 font-mono text-[9px] text-[#707070]">
                {formatMilliseconds(maximumMs)}
            </span>
            <div
                className="pointer-events-none absolute right-2.5 left-2.5 z-10 border-t border-dashed border-[#fbbf24]/60"
                style={{ bottom: `${16 + budgetBottom * 0.96}px` }}
            >
                <span className="absolute right-0 -top-3.5 bg-[#191919] pl-1 font-mono text-[9px] text-[#cda92e]">
                    {budgetMs.toFixed(2)} ms
                </span>
            </div>
            <div className="flex size-full items-end gap-px" aria-label="Captured frame times">
                {frames.map((frame) => {
                    const ratio = Math.max(0.02, Math.min(1, frame.durationMs / maximumMs));
                    const overBudget = frame.durationMs > budgetMs;
                    const severe = frame.durationMs > budgetMs * 1.5;
                    return (
                        <button
                            key={frame.frame}
                            type="button"
                            className={cn(
                                "min-w-px flex-1 border-0 p-0 opacity-85 outline-none transition-[opacity,filter] hover:opacity-100 focus-visible:ring-1 focus-visible:ring-white",
                                severe
                                    ? "bg-[#fb7185]"
                                    : overBudget
                                      ? "bg-[#fbbf24]"
                                      : "bg-[#4aa8ff]",
                                selectedFrame === frame.frame &&
                                    "z-20 opacity-100 ring-1 ring-white brightness-125",
                            )}
                            style={{ height: `${ratio * 100}%` }}
                            aria-label={`Frame ${frame.frame}, ${formatMilliseconds(frame.durationMs)}`}
                            title={`Frame ${frame.frame} · ${formatMilliseconds(frame.durationMs)}`}
                            onClick={() => onSelect(frame.frame)}
                        />
                    );
                })}
            </div>
            <span className="absolute bottom-0.5 left-2.5 font-mono text-[9px] text-[#606060]">
                {frames.length > 0 ? `Frame ${frames[0]?.frame}` : "No frames"}
            </span>
            <span className="absolute right-2.5 bottom-0.5 font-mono text-[9px] text-[#606060]">
                {frames.length > 0 ? `Frame ${frames.at(-1)?.frame}` : ""}
            </span>
        </div>
    );
}

function MetricCard({ label, value, detail }: { label: string; value: string; detail?: string }) {
    return (
        <div className="min-w-0 rounded border border-[#343434] bg-[#202020] px-3 py-2.5">
            <div className="text-[10px] font-bold tracking-[0.08em] text-muted-foreground uppercase">
                {label}
            </div>
            <div className="mt-1 truncate font-mono text-[18px] font-semibold text-[#d8e8f8]">
                {value}
            </div>
            {detail && <div className="mt-0.5 truncate text-[10px] text-[#6f7782]">{detail}</div>}
        </div>
    );
}

function HotspotList({ title, entries }: { title: string; entries: ProfileEntry[] }) {
    const hotspots = [...entries].sort((left, right) => right.selfMs - left.selfMs).slice(0, 5);
    return (
        <section className="min-w-0 rounded border border-[#343434] bg-[#202020]">
            <header className="border-b border-[#343434] px-3 py-2 text-[10px] font-bold tracking-[0.08em] text-muted-foreground uppercase">
                {title}
            </header>
            {hotspots.length === 0 ? (
                <div className="px-3 py-5 text-center text-[11px] text-muted-foreground">No samples</div>
            ) : (
                <div className="divide-y divide-[#303030]">
                    {hotspots.map((entry) => (
                        <div
                            key={`${entry.scheduleId}:${entry.systemId}:${entry.file}:${entry.line}:${entry.name}`}
                            className="grid grid-cols-[minmax(0,1fr)_auto] gap-3 px-3 py-1.5 text-[11px]"
                        >
                            <span className="truncate" title={entry.name}>{entry.name}</span>
                            <span className="font-mono text-[#8fc8ff]">{formatMilliseconds(entry.selfMs)}</span>
                        </div>
                    ))}
                </div>
            )}
        </section>
    );
}

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
                "w-full border-0 bg-transparent px-2 py-1.5 text-left text-[10px] font-bold tracking-[0.06em] text-[#7f8791] uppercase hover:text-foreground",
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
            <table className="w-full min-w-[720px] table-fixed border-collapse font-mono text-[11px]">
                <thead className="sticky top-0 z-10 bg-[#242424]">
                    <tr className="border-b border-[#363636]">
                        <th className="w-[38%]"><SortButton label="Name" active={sortKey === "name"} direction={direction} onClick={() => toggleSort("name")} /></th>
                        <th><SortButton label="Total" active={sortKey === "totalMs"} direction={direction} onClick={() => toggleSort("totalMs")} /></th>
                        <th><SortButton label="Self" active={sortKey === "selfMs"} direction={direction} onClick={() => toggleSort("selfMs")} /></th>
                        <th><SortButton label="Calls" active={sortKey === "count"} direction={direction} onClick={() => toggleSort("count")} /></th>
                        <th><SortButton label="Mean" active={sortKey === "meanMs"} direction={direction} onClick={() => toggleSort("meanMs")} /></th>
                        <th><SortButton label="Max" active={sortKey === "maxMs"} direction={direction} onClick={() => toggleSort("maxMs")} /></th>
                    </tr>
                </thead>
                <tbody>
                    {rows.map((entry) => (
                        <tr
                            key={`${entry.scheduleId}:${entry.systemId}:${entry.file}:${entry.line}:${entry.name}`}
                            className="border-b border-[#292929] text-[#b8c0ca] hover:bg-[#252c33]"
                            title={entry.file ? `${entry.file}:${entry.line}` : entry.functionName}
                        >
                            <td className="px-2 py-1.5">
                                <div className="truncate text-[#d0d5db]">{entry.name}</div>
                                {(entry.scheduleName || entry.file) && (
                                    <div className="truncate text-[9px] text-[#68717c]">
                                        {entry.scheduleName || `${entry.file}:${entry.line}`}
                                    </div>
                                )}
                            </td>
                            <td className="px-2 py-1.5">{formatMilliseconds(entry.totalMs)}</td>
                            <td className="px-2 py-1.5 text-[#8fc8ff]">{formatMilliseconds(entry.selfMs)}</td>
                            <td className="px-2 py-1.5">{formatCount(entry.count)}</td>
                            <td className="px-2 py-1.5">{formatMilliseconds(entry.meanMs)}</td>
                            <td className="px-2 py-1.5">{formatMilliseconds(entry.maxMs)}</td>
                        </tr>
                    ))}
                </tbody>
            </table>
        </div>
    );
}

function GpuEntriesTable({ entries, filter }: { entries: GpuProfileEntry[]; filter: string }) {
    const [sortKey, setSortKey] = useState<GpuSortKey>("totalMs");
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
            <table className="w-full min-w-[680px] table-fixed border-collapse font-mono text-[11px]">
                <thead className="sticky top-0 z-10 bg-[#242424]">
                    <tr className="border-b border-[#363636]">
                        <th className="w-[38%]"><SortButton label="Name" active={sortKey === "name"} direction={direction} onClick={() => toggleSort("name")} /></th>
                        <th><SortButton label="Latest" active={sortKey === "latestMs"} direction={direction} onClick={() => toggleSort("latestMs")} /></th>
                        <th><SortButton label="Total" active={sortKey === "totalMs"} direction={direction} onClick={() => toggleSort("totalMs")} /></th>
                        <th><SortButton label="Calls" active={sortKey === "count"} direction={direction} onClick={() => toggleSort("count")} /></th>
                        <th><SortButton label="Mean" active={sortKey === "meanMs"} direction={direction} onClick={() => toggleSort("meanMs")} /></th>
                        <th><SortButton label="Max" active={sortKey === "maxMs"} direction={direction} onClick={() => toggleSort("maxMs")} /></th>
                    </tr>
                </thead>
                <tbody>
                    {rows.map((entry) => (
                        <tr key={entry.name} className="border-b border-[#292929] text-[#b8c0ca] hover:bg-[#252c33]">
                            <td className="truncate px-2 py-1.5 text-[#d0d5db]" title={entry.name}>{entry.name}</td>
                            <td className="px-2 py-1.5 text-[#bfa7ff]">{formatMilliseconds(entry.latestMs)}</td>
                            <td className="px-2 py-1.5">{formatMilliseconds(entry.totalMs)}</td>
                            <td className="px-2 py-1.5">{formatCount(entry.count)}</td>
                            <td className="px-2 py-1.5">{formatMilliseconds(entry.meanMs)}</td>
                            <td className="px-2 py-1.5">{formatMilliseconds(entry.maxMs)}</td>
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

export function ProfilerPanel({ runtimeState, sessionId, inspect }: ProfilerPanelProps) {
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
    const [activeTab, setActiveTab] = useState<ProfilerTab>("overview");
    const [filter, setFilter] = useState("");
    const [controlBusy, setControlBusy] = useState(false);
    const [error, setError] = useState("");
    const [lastUpdated, setLastUpdated] = useState("");
    const [refreshToken, setRefreshToken] = useState(0);
    const retainedSessionId = useRef<string | null>(null);
    const frameDetailsRef = useRef<Map<number, ProfileFrameDetail>>(new Map());

    const clearLocalData = useCallback(() => {
        setSummary(null);
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
        if (runtimeState !== "running" || !sessionId) return;
        let cancelled = false;
        let polling = false;

        const poll = async (): Promise<void> => {
            if (polling) return;
            polling = true;
            try {
                const results = await Promise.allSettled([
                    inspectProfileSummary(inspect),
                    inspectProfileFrameHistory(inspect),
                    inspectGpuProfileSummary(inspect),
                    controlProfiling(inspect, "status"),
                ]);
                if (cancelled) return;

                const [summaryResult, historyResult, gpuResult, statusResult] = results;
                let detailFailure = "";
                let nextFrameDetails = frameDetailsRef.current;
                if (historyResult.status === "fulfilled") {
                    const retainedFrames = new Set(
                        historyResult.value.frames.map((frame) => frame.frame),
                    );
                    nextFrameDetails = new Map(
                        [...frameDetailsRef.current].filter(
                            ([frame]) => retainedFrames.has(frame) || frame === selectedFrame,
                        ),
                    );
                    const missingFrames = historyResult.value.frames
                        .map((frame) => frame.frame)
                        .filter((frame) => !nextFrameDetails.has(frame));
                    const selectedIsMissing =
                        selectedFrame !== null && missingFrames.includes(selectedFrame);
                    const recentMissing = missingFrames
                        .filter((frame) => frame !== selectedFrame)
                        .slice(-(frameDetailBatchSize - (selectedIsMissing ? 1 : 0)));
                    const detailFrames = selectedIsMissing
                        ? [selectedFrame, ...recentMissing]
                        : recentMissing;
                    if (detailFrames.length > 0) {
                        try {
                            const response = await resolveProfileFrameDetails(
                                await inspectProfileFrameDetails(inspect, detailFrames),
                            );
                            for (const detail of response.details) {
                                nextFrameDetails.set(detail.frame, detail);
                            }
                        } catch (caught) {
                            detailFailure = errorMessage(caught);
                        }
                    }
                }

                const resolvedSummary =
                    summaryResult.status === "fulfilled"
                        ? await resolveProfileSummary(summaryResult.value)
                        : null;
                if (cancelled) return;

                if (resolvedSummary) setSummary(resolvedSummary);
                if (historyResult.status === "fulfilled") {
                    setHistory(historyResult.value);
                    frameDetailsRef.current = nextFrameDetails;
                    setFrameDetails(new Map(nextFrameDetails));
                }
                if (gpuResult.status === "fulfilled") setGpuSummary(gpuResult.value);
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
    }, [inspect, refreshToken, runtimeState, selectedFrame, sessionId]);

    const runControl = async (action: ProfileControlAction): Promise<void> => {
        if (controlBusy) return;
        if (action === "clear" && (runtimeState !== "running" || !sessionId)) {
            clearLocalData();
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
    const selectedDetail = analysisFrame === null ? undefined : frameDetails.get(analysisFrame);
    const selectedFrameIndex = frames.findIndex((frame) => frame.frame === analysisFrame);
    const cpuSystems = selectedDetail?.systems ?? [];
    const cpuZones = selectedDetail?.zones ?? [];
    const dataAvailable = summary !== null || history !== null || gpuSummary !== null;
    const controlsDisabled = runtimeState !== "running" || !sessionId || controlBusy;
    const tabs: readonly { id: ProfilerTab; label: string; count?: number }[] = [
        { id: "overview", label: "Overview" },
        { id: "systems", label: "CPU Systems", count: cpuSystems.length },
        { id: "zones", label: "CPU Zones", count: cpuZones.length },
        { id: "gpu", label: "GPU Capture", count: gpuSummary?.entries.length },
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
                <Button variant="ghost" size="icon" className="size-6" aria-label="Clear profiling capture" disabled={controlBusy || (!dataAvailable && runtimeState !== "running")} onClick={() => void runControl("clear")}>
                    <Trash2 size={12} />
                </Button>
                <Button variant="ghost" size="icon" className="size-6" aria-label="Refresh profiler" disabled={runtimeState !== "running" || !sessionId} onClick={() => setRefreshToken((current) => current + 1)}>
                    <RefreshCw size={12} className={controlBusy ? "animate-spin" : ""} />
                </Button>
                <span className="h-4 border-l border-[#4a4a4a]" />
                <Button
                    variant="ghost"
                    size="icon"
                    className="size-6"
                    aria-label="Select previous profile frame"
                    disabled={selectedFrameIndex <= 0}
                    onClick={() => setSelectedFrame(frames[selectedFrameIndex - 1]?.frame ?? null)}
                >
                    <ChevronLeft size={13} />
                </Button>
                <Button
                    variant="ghost"
                    size="icon"
                    className="size-6"
                    aria-label="Select next profile frame"
                    disabled={
                        selectedFrame === null ||
                        selectedFrameIndex < 0 ||
                        selectedFrameIndex >= frames.length - 1
                    }
                    onClick={() => setSelectedFrame(frames[selectedFrameIndex + 1]?.frame ?? null)}
                >
                    <ChevronRight size={13} />
                </Button>
                <Button
                    variant={selectedFrame === null ? "secondary" : "ghost"}
                    size="sm"
                    className="h-6 px-2"
                    disabled={frames.length === 0}
                    onClick={() => setSelectedFrame(null)}
                >
                    <SkipForward size={12} /> Live
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
                        <strong className="text-xs text-[#b7bdc8]">Profiler is waiting for the runtime</strong>
                        <span>Start the game runtime to collect CPU and GPU samples.</span>
                    </div>
                </PanelEmptyState>
            ) : (
                <>
                    <FrameChart
                        frames={frames}
                        targetFps={targetFps}
                        selectedFrame={analysisFrame}
                        onSelect={setSelectedFrame}
                    />
                    <div className="flex h-[31px] shrink-0 items-center border-b border-[#171717] bg-[#222]">
                        {tabs.map((tab) => (
                            <button
                                key={tab.id}
                                type="button"
                                className={cn(
                                    "h-full border-0 border-b-2 border-transparent bg-transparent px-3 text-[10px] font-semibold text-muted-foreground hover:bg-white/5 hover:text-foreground",
                                    activeTab === tab.id && "border-primary text-foreground",
                                )}
                                onClick={() => setActiveTab(tab.id)}
                            >
                                {tab.label}
                                {tab.count !== undefined && <span className="ml-1 text-[#5f6975]">{tab.count}</span>}
                            </button>
                        ))}
                        {activeTab !== "overview" && (
                            <label className="relative ml-auto mr-2 block w-[180px] max-w-[30%]">
                                <Search className="pointer-events-none absolute top-1/2 left-2 size-3 -translate-y-1/2 text-[#66707b]" />
                                <input
                                    type="search"
                                    className="h-6 w-full rounded border border-[#3c3c3c] bg-[#191919] pr-2 pl-6 text-[10px] outline-none placeholder:text-[#606060] focus:border-primary/60"
                                    value={filter}
                                    placeholder="Filter samples"
                                    onChange={(event) => setFilter(event.target.value)}
                                />
                            </label>
                        )}
                    </div>

                    {activeTab === "overview" && (
                        <div className="min-h-0 flex-1 overflow-auto p-2.5">
                            <div className="grid gap-2 [grid-template-columns:repeat(auto-fit,minmax(112px,1fr))]">
                                <MetricCard label="FPS" value={summary?.frameStats.available ? summary.frameStats.fps.toFixed(1) : "—"} detail={`${targetFps} FPS target`} />
                                <MetricCard label={selectedFrame === null ? "Live frame" : `Frame ${selectedFrame}`} value={selectedSample || selectedDetail ? formatMilliseconds(selectedSample?.durationMs ?? selectedDetail?.durationMs ?? Number.NaN) : "—"} detail={selectedFrame === null ? "Following latest" : "Pinned selection"} />
                                <MetricCard label="Average frame" value={summary?.frameStats.available ? formatMilliseconds(summary.frameStats.averageFrameMs) : "—"} />
                                <MetricCard label="Captured" value={formatCount(frames.length)} detail="600-frame rolling history" />
                            </div>
                            <div className="mt-2 grid gap-2 [grid-template-columns:repeat(auto-fit,minmax(240px,1fr))]">
                                <HotspotList title="Frame CPU system hotspots · self" entries={cpuSystems} />
                                <HotspotList title="Frame CPU zone hotspots · self" entries={cpuZones} />
                            </div>
                            {analysisFrame !== null && !selectedDetail && summary?.available !== false && (
                                <div className="mt-2 rounded border border-[#3d5368] bg-[#22313f]/45 px-3 py-2 text-[11px] text-[#9fc8e8]">
                                    {runtimeState === "running"
                                        ? "Loading CPU details for this frame…"
                                        : "CPU details for this frame were not cached before the runtime stopped."}
                                </div>
                            )}
                            {summary?.available === false && (
                                <div className="mt-2 rounded border border-[#705b20] bg-[#3a3015]/45 px-3 py-2 text-[11px] text-[#d8bd62]">
                                    CPU profiling is unavailable. Build the Editor Runtime with <code className="font-mono">--profile_summary=y</code>.
                                </div>
                            )}
                        </div>
                    )}
                    {activeTab === "systems" && <CpuEntriesTable entries={cpuSystems} filter={filter} />}
                    {activeTab === "zones" && <CpuEntriesTable entries={cpuZones} filter={filter} />}
                    {activeTab === "gpu" && (
                        <div className="flex min-h-0 flex-1 flex-col">
                            <div className="shrink-0 border-b border-[#343434] bg-[#202020] px-3 py-1.5 text-[10px] text-[#78838f]">
                                GPU timestamps are capture-wide because query results arrive asynchronously.
                            </div>
                            <GpuEntriesTable entries={gpuSummary?.entries ?? []} filter={filter} />
                        </div>
                    )}
                </>
            )}

            <PanelStatus>
                <span className={cn("truncate", error && "text-[#ff9aaa]")} title={error || undefined}>
                    {error || (runtimeState === "running" ? captureLabel(captureStatus) : "Runtime stopped · capture frozen")}
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
