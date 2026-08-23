import type {
    RuntimeEvent,
    RuntimeEventListener,
    RuntimeProjectFile,
    RuntimeSnapshot,
} from "./types";

const runtimeSource = "../sample-browser-project.html";
const startupTimeoutMilliseconds = 60_000;
const statusPollMilliseconds = 100;

interface RuntimeMessage {
    source: "entisium-runtime";
    channelId: string;
    type: string;
    level?: string;
    message?: unknown;
}
function runtimeRequestId(): string {
    return globalThis.crypto?.randomUUID?.() ?? `${Date.now()}-${Math.random()}`;
}

function isRuntimeMessage(value: unknown): value is RuntimeMessage {
    if (!value || typeof value !== "object") return false;
    const message = value as Record<string, unknown>;
    return (
        message.source === "entisium-runtime" &&
        typeof message.channelId === "string" &&
        typeof message.type === "string"
    );
}

export class WasmRuntimeController {
    private snapshot: RuntimeSnapshot = {
        state: "stopped",
        detail: "stopped",
        script: "—",
        frame: "—",
        session: null,
    };

    private readonly listeners = new Set<RuntimeEventListener>();
    private frameElement: HTMLIFrameElement | null = null;
    private pollTimer: number | null = null;
    private startupTimer: number | null = null;
    private listening = false;

    getSnapshot(): RuntimeSnapshot {
        return { ...this.snapshot };
    }

    subscribe(listener: RuntimeEventListener): () => void {
        this.listeners.add(listener);
        return () => this.listeners.delete(listener);
    }

    attachFrame(frame: HTMLIFrameElement | null): void {
        this.frameElement = frame;
    }

    async start(files: RuntimeProjectFile[], force = false): Promise<void> {
        if (!force && (this.snapshot.state === "starting" || this.snapshot.state === "running")) {
            return;
        }

        this.stopMonitoring();
        const channelId = runtimeRequestId();
        this.updateSnapshot({
            state: "starting",
            detail: "starting",
            script: "—",
            frame: "—",
            session: {
                channelId,
                files,
                source: `${runtimeSource}?entisium-editor-channel=${encodeURIComponent(channelId)}&dev=${Date.now()}`,
            },
        });
        this.emitLog("info", "runtime", "creating isolated runtime");
        this.startMonitoring();
    }

    stop(reason = "runtime stopped", log = true): void {
        this.stopMonitoring();
        this.frameElement = null;
        this.updateSnapshot({
            state: "stopped",
            detail: "stopped",
            script: "—",
            frame: "—",
            session: null,
        });
        if (log) this.emitLog("info", "runtime", reason);
    }

    async restart(files: RuntimeProjectFile[]): Promise<void> {
        this.stop("restarting runtime");
        await this.start(files, true);
    }

    dispose(): void {
        this.stopMonitoring();
        this.frameElement = null;
        this.listeners.clear();
    }

    private readonly onWindowMessage = (event: MessageEvent): void => {
        const session = this.snapshot.session;
        if (
            !session ||
            event.source !== this.frameElement?.contentWindow ||
            event.origin !== location.origin ||
            !isRuntimeMessage(event.data) ||
            event.data.channelId !== session.channelId
        ) {
            return;
        }

        const message = event.data;
        if (message.type === "project.request") {
            this.frameElement?.contentWindow?.postMessage(
                {
                    source: "entisium-editor",
                    channelId: session.channelId,
                    type: "project.files",
                    files: session.files,
                },
                location.origin,
            );
        } else if (message.type === "project.applied") {
            this.emitLog("info", "runtime", "project files applied");
        } else if (message.type === "runtime.log") {
            this.emitLog(
                message.level === "error" ? "error" : "info",
                "game",
                String(message.message ?? ""),
            );
        } else if (message.type === "runtime.error") {
            this.emitLog("error", "runtime", String(message.message ?? "runtime failed"));
            this.updateSnapshot({ state: "failed", detail: "failed" });
        }
    };

    private startMonitoring(): void {
        if (!this.listening) {
            window.addEventListener("message", this.onWindowMessage);
            this.listening = true;
        }
        this.pollTimer = window.setInterval(() => this.pollRuntimeStatus(), statusPollMilliseconds);
        this.startupTimer = window.setTimeout(() => {
            if (this.snapshot.state !== "starting") return;
            this.updateSnapshot({ state: "failed", detail: "startup timed out" });
            this.emitLog("error", "runtime", "startup timed out");
        }, startupTimeoutMilliseconds);
    }

    private stopMonitoring(): void {
        if (this.listening) {
            window.removeEventListener("message", this.onWindowMessage);
            this.listening = false;
        }
        if (this.pollTimer !== null) {
            window.clearInterval(this.pollTimer);
            this.pollTimer = null;
        }
        if (this.startupTimer !== null) {
            window.clearTimeout(this.startupTimer);
            this.startupTimer = null;
        }
    }

    private pollRuntimeStatus(): void {
        try {
            const data = this.frameElement?.contentDocument?.documentElement.dataset;
            if (!data) return;

            const patch: Partial<RuntimeSnapshot> = {
                script: data.entisiumProjectScript ?? "—",
                frame: data.entisiumProjectFramePresented === "true" ? "presented" : "—",
            };
            if (data.entisiumProjectStatus === "web project presented") {
                patch.state = "running";
                patch.detail = "running";
                document.documentElement.dataset.entisiumEditorProjectStatus = data.entisiumProjectStatus;
            } else if (data.entisiumProjectStatus?.includes("failed")) {
                patch.state = "failed";
                patch.detail = data.entisiumProjectStatus;
            }
            this.updateSnapshot(patch);
        } catch (error) {
            this.emitLog(
                "error",
                "editor",
                error instanceof Error ? error.message : String(error),
            );
        }
    }

    private updateSnapshot(patch: Partial<RuntimeSnapshot>): void {
        const next = { ...this.snapshot, ...patch };
        if (
            next.state === this.snapshot.state &&
            next.detail === this.snapshot.detail &&
            next.script === this.snapshot.script &&
            next.frame === this.snapshot.frame &&
            next.session === this.snapshot.session
        ) {
            return;
        }
        this.snapshot = next;
        this.emit({ type: "snapshot", snapshot: this.getSnapshot() });
    }

    private emitLog(
        level: "info" | "error",
        source: "editor" | "game" | "runtime",
        message: string,
    ): void {
        this.emit({ type: "log", level, source, message });
    }

    private emit(event: RuntimeEvent): void {
        for (const listener of this.listeners) listener(event);
    }
}
