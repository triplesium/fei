import { spawn, type ChildProcessWithoutNullStreams } from "node:child_process";
import { access } from "node:fs/promises";
import type { IncomingMessage } from "node:http";
import type { Duplex } from "node:stream";
import { WebSocket, WebSocketServer, type RawData } from "ws";

const maximumMessageBytes = 16 * 1024 * 1024;
const headerDelimiter = Buffer.from("\r\n\r\n", "ascii");

export interface LuauLspGatewayOptions {
    executable: string;
    arguments?: readonly string[];
    workspaceRoot(): Promise<string>;
    log?(message: string): void;
}

function rejectUpgrade(socket: Duplex, status: number, reason: string): void {
    if (!socket.destroyed) {
        socket.end(
            `HTTP/1.1 ${status} ${reason}\r\nConnection: close\r\nContent-Length: 0\r\n\r\n`,
        );
    }
}

function messageBuffer(data: RawData): Buffer {
    if (Buffer.isBuffer(data)) return data;
    if (data instanceof ArrayBuffer) return Buffer.from(data);
    if (Array.isArray(data)) return Buffer.concat(data);
    throw new Error("Unsupported WebSocket message representation.");
}

class LuauLspSession {
    private output = Buffer.alloc(0);
    private expectedBodyBytes: number | undefined;
    private closed = false;

    constructor(
        private readonly socket: WebSocket,
        private readonly child: ChildProcessWithoutNullStreams,
        private readonly log: (message: string) => void,
        private readonly onClose: () => void,
    ) {
        socket.on("message", (data, isBinary) => this.receive(data, isBinary));
        socket.once("close", () => this.close());
        socket.once("error", (error) => this.fail(`WebSocket error: ${error.message}`));

        child.stdout.on("data", (chunk: Buffer) => this.receiveProcessOutput(chunk));
        child.stderr.setEncoding("utf8");
        child.stderr.on("data", (chunk: string) => {
            for (const line of chunk.split(/\r?\n/)) {
                if (line) this.log(`[entisium-lsp] ${line}`);
            }
        });
        child.stdin.on("error", (error) => this.fail(`LSP stdin failed: ${error.message}`));
        child.once("error", (error) => this.fail(`Unable to start entisium-lsp: ${error.message}`));
        child.once("exit", (code, signal) => {
            if (!this.closed) {
                this.fail(
                    `entisium-lsp exited unexpectedly (${signal ?? `code ${code ?? "unknown"}`}).`,
                );
            }
        });
    }

    private receive(data: RawData, isBinary: boolean): void {
        if (this.closed) return;
        const body = messageBuffer(data);
        if (isBinary || body.byteLength > maximumMessageBytes) {
            this.fail("Invalid LSP WebSocket message.");
            return;
        }
        try {
            const parsed = JSON.parse(body.toString("utf8"));
            if (!parsed || typeof parsed !== "object" || Array.isArray(parsed)) {
                throw new Error("Expected a JSON-RPC object.");
            }
        } catch (error) {
            this.fail(`Invalid LSP JSON: ${error instanceof Error ? error.message : String(error)}`);
            return;
        }

        const header = Buffer.from(`Content-Length: ${body.byteLength}\r\n\r\n`, "ascii");
        this.child.stdin.write(Buffer.concat([header, body]));
    }

    private receiveProcessOutput(chunk: Buffer): void {
        if (this.closed) return;
        this.output = Buffer.concat([this.output, chunk]);
        while (!this.closed) {
            if (this.expectedBodyBytes === undefined) {
                const delimiter = this.output.indexOf(headerDelimiter);
                if (delimiter < 0) {
                    if (this.output.byteLength > 8 * 1024) this.fail("LSP response header is too large.");
                    return;
                }
                const header = this.output.subarray(0, delimiter).toString("ascii");
                this.output = this.output.subarray(delimiter + headerDelimiter.byteLength);
                const match = /(?:^|\r\n)Content-Length:\s*(\d+)(?:\r\n|$)/i.exec(header);
                const length = match ? Number.parseInt(match[1], 10) : Number.NaN;
                if (!Number.isSafeInteger(length) || length < 0 || length > maximumMessageBytes) {
                    this.fail("Invalid LSP Content-Length header.");
                    return;
                }
                this.expectedBodyBytes = length;
            }

            if (this.output.byteLength < this.expectedBodyBytes) return;
            const message = this.output.subarray(0, this.expectedBodyBytes);
            this.output = this.output.subarray(this.expectedBodyBytes);
            this.expectedBodyBytes = undefined;
            if (this.socket.readyState === WebSocket.OPEN) {
                this.socket.send(message.toString("utf8"));
            }
        }
    }

    private fail(message: string): void {
        this.log(message);
        if (this.socket.readyState === WebSocket.OPEN) {
            this.socket.close(1011, message.slice(0, 120));
        }
        this.close();
    }

    close(): void {
        if (this.closed) return;
        this.closed = true;
        if (this.socket.readyState === WebSocket.OPEN) this.socket.close(1001, "LSP session closed");
        if (!this.child.killed) this.child.kill();
        this.onClose();
    }
}

export class LuauLspGateway {
    private readonly webSockets = new WebSocketServer({
        noServer: true,
        maxPayload: maximumMessageBytes,
    });
    private readonly sessions = new Set<LuauLspSession>();

    constructor(private readonly options: LuauLspGatewayOptions) {}

    async handleUpgrade(request: IncomingMessage, socket: Duplex, head: Buffer): Promise<void> {
        try {
            const workspaceRoot = await this.options.workspaceRoot();
            await access(this.options.executable);
            this.webSockets.handleUpgrade(request, socket, head, (webSocket) => {
                const child = spawn(
                    this.options.executable,
                    [...(this.options.arguments ?? ["--stdio"])],
                    {
                        cwd: workspaceRoot,
                        windowsHide: true,
                        stdio: ["pipe", "pipe", "pipe"],
                    },
                );
                let session: LuauLspSession;
                session = new LuauLspSession(
                    webSocket,
                    child,
                    (message) => (this.options.log ?? console.error)(message),
                    () => this.sessions.delete(session),
                );
                this.sessions.add(session);
                this.webSockets.emit("connection", webSocket, request);
            });
        } catch (error) {
            (this.options.log ?? console.error)(
                `[entisium editor] Luau LSP unavailable: ${
                    error instanceof Error ? error.message : String(error)
                }`,
            );
            rejectUpgrade(socket, 503, "Service Unavailable");
        }
    }

    dispose(): void {
        for (const session of [...this.sessions]) session.close();
        this.webSockets.close();
    }
}
