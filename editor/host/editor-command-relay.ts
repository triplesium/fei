import { randomUUID } from "node:crypto";
import type { IncomingMessage, ServerResponse } from "node:http";
import type { EditorCommandRequest } from "./editor-tool-catalog.js";

export interface EditorCommandResponse {
    requestId: string;
    ok: boolean;
    value?: unknown;
    error?: { code: string; message: string };
}

interface PendingCommand {
    client: ServerResponse;
    resolve(response: EditorCommandResponse): void;
    reject(error: Error): void;
    timeout: NodeJS.Timeout;
}

export class EditorCommandRelay {
    private readonly clients: ServerResponse[] = [];
    private readonly pending = new Map<string, PendingCommand>();

    attach(request: IncomingMessage, response: ServerResponse): void {
        response.writeHead(200, {
            "Content-Type": "text/event-stream; charset=utf-8",
            "Cache-Control": "no-cache, no-transform",
            Connection: "keep-alive",
        });
        response.flushHeaders();
        response.write(`data: ${JSON.stringify({ type: "ready" })}\n\n`);
        this.clients.push(response);
        const heartbeat = setInterval(() => {
            if (!response.destroyed) response.write(": heartbeat\n\n");
        }, 15_000);
        request.once("close", () => {
            clearInterval(heartbeat);
            const index = this.clients.indexOf(response);
            if (index >= 0) this.clients.splice(index, 1);
            for (const [id, command] of this.pending) {
                if (command.client !== response) continue;
                clearTimeout(command.timeout);
                command.reject(new Error("The Editor command connection closed."));
                this.pending.delete(id);
            }
        });
    }

    invoke(request: EditorCommandRequest, timeoutMs = 30_000): Promise<EditorCommandResponse> {
        let client: ServerResponse | undefined;
        for (let index = this.clients.length - 1; index >= 0; --index) {
            const candidate = this.clients[index];
            if (!candidate?.destroyed) {
                client = candidate;
                break;
            }
        }
        if (!client) {
            return Promise.reject(
                new Error("No Editor page is connected. Open the Editor before calling MCP tools."),
            );
        }
        const id = randomUUID();
        return new Promise((resolve, reject) => {
            const timeout = setTimeout(() => {
                this.pending.delete(id);
                reject(new Error(`Editor command timed out: ${request.type ?? "unknown"}`));
            }, timeoutMs);
            this.pending.set(id, { client, resolve, reject, timeout });
            client.write(
                `data: ${JSON.stringify({ type: "command", id, request })}\n\n`,
                (error?: Error | null) => {
                    if (!error) return;
                    clearTimeout(timeout);
                    this.pending.delete(id);
                    reject(error);
                },
            );
        });
    }

    complete(id: string, response: EditorCommandResponse): boolean {
        const command = this.pending.get(id);
        if (!command) return false;
        clearTimeout(command.timeout);
        this.pending.delete(id);
        command.resolve(response);
        return true;
    }

    dispose(): void {
        for (const command of this.pending.values()) {
            clearTimeout(command.timeout);
            command.reject(new Error("The Editor Host is shutting down."));
        }
        this.pending.clear();
        for (const client of this.clients) client.end();
        this.clients.length = 0;
    }
}
