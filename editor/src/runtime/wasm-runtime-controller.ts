import type {
    RuntimeEvent,
    RuntimeEventListener,
    RuntimeProjectFile,
    RuntimeSnapshot,
} from "./types";

const runtimeSource = "/runtime/index.html";
const startupTimeoutMilliseconds = 60_000;
const statusPollMilliseconds = 100;

interface RuntimeMessage {
    source: "entisium-runtime";
    channelId: string;
    type: string;
    level?: string;
    message?: unknown;
    requestId?: string;
    mimeType?: unknown;
    data?: unknown;
    width?: unknown;
    height?: unknown;
    error?: unknown;
    errorKind?: unknown;
    value?: unknown;
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
    private readonly heldKeys = new Set<string>();
    private readonly heldButtons = new Set<number>();
    private pointer = { x: 0.5, y: 0.5 };
    private readonly captureRequests = new Map<
        string,
        {
            resolve: (capture: RuntimeCapture) => void;
            reject: (error: Error) => void;
            timeout: ReturnType<typeof globalThis.setTimeout>;
        }
    >();
    private readonly inspectionRequests = new Map<
        string,
        {
            resolve: (value: unknown) => void;
            reject: (error: Error) => void;
            timeout: ReturnType<typeof globalThis.setTimeout>;
        }
    >();

    getSnapshot(): RuntimeSnapshot {
        return { ...this.snapshot };
    }

    subscribe(listener: RuntimeEventListener): () => void {
        this.listeners.add(listener);
        return () => this.listeners.delete(listener);
    }

    attachFrame(frame: HTMLIFrameElement | null): void {
        this.frameElement = frame;
        if (!frame) {
            this.heldKeys.clear();
            this.heldButtons.clear();
        }
    }

    capture(): Promise<RuntimeCapture> {
        const { view } = this.runtimeElements();
        const session = this.snapshot.session;
        if (!session) throw new Error("The runtime session is unavailable.");
        const requestId = runtimeRequestId();
        return new Promise((resolve, reject) => {
            const timeout = globalThis.setTimeout(() => {
                this.captureRequests.delete(requestId);
                reject(new Error("Runtime frame capture timed out."));
            }, 5000);
            this.captureRequests.set(requestId, { resolve, reject, timeout });
            view.postMessage(
                {
                    source: "entisium-editor",
                    channelId: session.channelId,
                    type: "runtime.capture",
                    requestId,
                },
                location.origin,
            );
        });
    }

    inspect(provider: string, schema: string, payload: unknown): Promise<unknown> {
        const { view } = this.runtimeElements();
        const session = this.snapshot.session;
        if (!session) throw new Error("The runtime session is unavailable.");
        if (!provider || !schema) {
            throw new Error("Runtime inspection requires a provider and schema.");
        }
        const requestId = runtimeRequestId();
        return new Promise((resolve, reject) => {
            const timeout = globalThis.setTimeout(() => {
                this.inspectionRequests.delete(requestId);
                reject(new Error(`Runtime inspection '${provider}' timed out.`));
            }, 5000);
            this.inspectionRequests.set(requestId, { resolve, reject, timeout });
            view.postMessage(
                {
                    source: "entisium-editor",
                    channelId: session.channelId,
                    type: "runtime.inspect",
                    requestId,
                    provider,
                    schema,
                    payload,
                },
                location.origin,
            );
        });
    }

    async key(code: string, action: string, durationMs = 80): Promise<unknown> {
        if (!/^(?:Key[A-Z]|Digit[0-9]|Arrow(?:Up|Down|Left|Right)|Space|Enter|Escape|Tab|Shift(?:Left|Right)|Control(?:Left|Right)|Alt(?:Left|Right))$/.test(code)) {
            throw new Error(`Unsupported runtime key code: ${code}`);
        }
        if (action === "press") {
            this.dispatchKey(code, true);
        } else if (action === "release") {
            this.dispatchKey(code, false);
        } else if (action === "tap") {
            this.dispatchKey(code, true);
            await delay(boundedDuration(durationMs, 80));
            this.dispatchKey(code, false);
        } else {
            throw new Error("Runtime key action must be press, release, or tap.");
        }
        return { code, action, held: [...this.heldKeys] };
    }

    async pointerInput(
        x: number,
        y: number,
        action: string,
        buttonName = "left",
        durationMs = 50,
    ): Promise<unknown> {
        if (!Number.isFinite(x) || !Number.isFinite(y) || x < 0 || x > 1 || y < 0 || y > 1) {
            throw new Error("Runtime pointer coordinates must be between 0 and 1.");
        }
        const button = { left: 0, middle: 1, right: 2 }[buttonName];
        if (button === undefined) throw new Error(`Unsupported pointer button: ${buttonName}`);
        this.pointer = { x, y };
        if (action === "move") {
            this.dispatchPointer("mousemove", button);
        } else if (action === "press") {
            this.heldButtons.add(button);
            this.dispatchPointer("mousedown", button);
        } else if (action === "release") {
            this.heldButtons.delete(button);
            this.dispatchPointer("mouseup", button);
        } else if (action === "click") {
            this.heldButtons.add(button);
            this.dispatchPointer("mousedown", button);
            await delay(boundedDuration(durationMs, 50));
            this.heldButtons.delete(button);
            this.dispatchPointer("mouseup", button);
        } else {
            throw new Error("Runtime pointer action must be move, press, release, or click.");
        }
        return { x, y, action, button: buttonName };
    }

    clearInput(): unknown {
        for (const code of [...this.heldKeys]) this.dispatchKey(code, false);
        for (const button of [...this.heldButtons]) {
            this.heldButtons.delete(button);
            this.dispatchPointer("mouseup", button);
        }
        return { cleared: true };
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
        if (this.frameElement) this.clearInput();
        this.cancelCaptureRequests(reason);
        this.cancelInspectionRequests(reason);
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
        } else if (message.type === "runtime.capture" && message.requestId) {
            const pending = this.captureRequests.get(message.requestId);
            if (!pending) return;
            this.captureRequests.delete(message.requestId);
            globalThis.clearTimeout(pending.timeout);
            if (typeof message.error === "string") {
                pending.reject(new Error(`Runtime frame capture failed: ${message.error}`));
                return;
            }
            if (
                message.mimeType !== "image/png" ||
                typeof message.data !== "string" ||
                typeof message.width !== "number" ||
                typeof message.height !== "number"
            ) {
                pending.reject(new Error("The runtime returned an invalid frame capture."));
                return;
            }
            pending.resolve({
                mimeType: message.mimeType,
                data: message.data,
                width: message.width,
                height: message.height,
            });
        } else if (message.type === "runtime.inspection" && message.requestId) {
            const pending = this.inspectionRequests.get(message.requestId);
            if (!pending) return;
            this.inspectionRequests.delete(message.requestId);
            globalThis.clearTimeout(pending.timeout);
            if (typeof message.error === "string") {
                const kind =
                    typeof message.errorKind === "string" ? `${message.errorKind}: ` : "";
                pending.reject(new Error(`Runtime inspection failed: ${kind}${message.error}`));
                return;
            }
            pending.resolve(message.value);
        }
    };

    private runtimeElements(): {
        view: Window;
        document: Document;
        canvas: HTMLCanvasElement;
    } {
        if (this.snapshot.state !== "running") {
            throw new Error("The runtime must be running before game interaction.");
        }
        const view = this.frameElement?.contentWindow;
        const document = this.frameElement?.contentDocument;
        const canvas = document?.querySelector<HTMLCanvasElement>("#canvas");
        if (!view || !document || !canvas) throw new Error("The runtime viewport is unavailable.");
        return { view, document, canvas };
    }

    private dispatchKey(code: string, down: boolean): void {
        const { view } = this.runtimeElements();
        const KeyboardEventConstructor = (
            view as unknown as { KeyboardEvent: typeof KeyboardEvent }
        ).KeyboardEvent;
        if (down) this.heldKeys.add(code);
        else this.heldKeys.delete(code);
        view.dispatchEvent(
            new KeyboardEventConstructor(down ? "keydown" : "keyup", {
                code,
                key: keyForCode(code),
                bubbles: true,
                cancelable: true,
            }),
        );
    }

    private dispatchPointer(type: "mousemove" | "mousedown" | "mouseup", button: number): void {
        const { view, canvas } = this.runtimeElements();
        const MouseEventConstructor = (
            view as unknown as { MouseEvent: typeof MouseEvent }
        ).MouseEvent;
        const bounds = canvas.getBoundingClientRect();
        const clientX = bounds.left + bounds.width * this.pointer.x;
        const clientY = bounds.top + bounds.height * this.pointer.y;
        canvas.dispatchEvent(
            new MouseEventConstructor(type, {
                bubbles: true,
                cancelable: true,
                clientX,
                clientY,
                button,
                buttons: mouseButtonsMask(this.heldButtons),
            }),
        );
    }

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

    private cancelCaptureRequests(reason: string): void {
        for (const pending of this.captureRequests.values()) {
            globalThis.clearTimeout(pending.timeout);
            pending.reject(new Error(`Runtime frame capture cancelled: ${reason}`));
        }
        this.captureRequests.clear();
    }

    private cancelInspectionRequests(reason: string): void {
        for (const pending of this.inspectionRequests.values()) {
            globalThis.clearTimeout(pending.timeout);
            pending.reject(new Error(`Runtime inspection cancelled: ${reason}`));
        }
        this.inspectionRequests.clear();
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

interface RuntimeCapture {
    mimeType: "image/png";
    data: string;
    width: number;
    height: number;
}

function boundedDuration(value: number, fallback: number): number {
    return Number.isFinite(value) ? Math.max(0, Math.min(5000, value)) : fallback;
}

function delay(durationMs: number): Promise<void> {
    return new Promise((resolve) => globalThis.setTimeout(resolve, durationMs));
}

function keyForCode(code: string): string {
    if (code.startsWith("Key")) return code.slice(3).toLowerCase();
    if (code.startsWith("Digit")) return code.slice(5);
    return {
        Space: " ",
        Enter: "Enter",
        Escape: "Escape",
        Tab: "Tab",
    }[code] ?? code;
}

function mouseButtonsMask(buttons: ReadonlySet<number>): number {
    let mask = 0;
    if (buttons.has(0)) mask |= 1;
    if (buttons.has(2)) mask |= 2;
    if (buttons.has(1)) mask |= 4;
    return mask;
}
