import { mkdir, mkdtemp, rm, writeFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { Client } from "@modelcontextprotocol/sdk/client/index.js";
import { StreamableHTTPClientTransport } from "@modelcontextprotocol/sdk/client/streamableHttp.js";
import { afterEach, describe, expect, it } from "vitest";
import { EncryptedCredentialStore } from "./credential-store.js";
import { createEditorHost } from "./server.js";

const temporaryDirectories: string[] = [];

afterEach(async () => {
    await Promise.all(
        temporaryDirectories.splice(0).map((directory) =>
            rm(directory, { recursive: true, force: true }),
        ),
    );
});

async function nextCommand(response: Response): Promise<{
    id: string;
    request: { type: string };
}> {
    if (!response.body) throw new Error("Editor command stream has no body.");
    const reader = response.body.pipeThrough(new TextDecoderStream()).getReader();
    let buffer = "";
    for (;;) {
        const chunk = await reader.read();
        if (chunk.done) throw new Error("Editor command stream closed.");
        buffer += chunk.value;
        let boundary = buffer.indexOf("\n\n");
        while (boundary >= 0) {
            const block = buffer.slice(0, boundary);
            buffer = buffer.slice(boundary + 2);
            const data = block
                .split("\n")
                .find((line) => line.startsWith("data:"))
                ?.slice(5)
                .trim();
            if (data) {
                const event = JSON.parse(data) as {
                    type: string;
                    id?: string;
                    request?: { type?: string };
                };
                if (event.type === "command" && event.id && event.request?.type) {
                    return {
                        id: event.id,
                        request: { type: event.request.type },
                    };
                }
            }
            boundary = buffer.indexOf("\n\n");
        }
    }
}

describe("Editor MCP server", () => {
    it("relays MCP tools through the connected Editor command API", async () => {
        const directory = await mkdtemp(join(tmpdir(), "entisium-editor-mcp-"));
        temporaryDirectories.push(directory);
        const distDirectory = join(directory, "dist");
        await mkdir(distDirectory);
        await writeFile(join(distDirectory, "index.html"), "<p>Entisium Editor</p>", "utf8");
        const host = createEditorHost({
            credentials: new EncryptedCredentialStore(join(directory, "credentials.json")),
            distDirectory,
            runtimeDirectory: distDirectory,
            port: 0,
        });
        const address = await host.listen();
        const baseUrl = `http://${address.host}:${address.port}`;
        const bootstrap = await fetch(`${baseUrl}/api/v1/bootstrap`).then((response) =>
            response.json() as Promise<{ token: string }>,
        );
        const editorAbort = new AbortController();
        const commandStream = await fetch(`${baseUrl}/api/v1/editor/commands`, {
            headers: { Authorization: `Bearer ${bootstrap.token}` },
            signal: editorAbort.signal,
        });
        const client = new Client({ name: "entisium-editor-test", version: "1.0.0" });
        const transport = new StreamableHTTPClientTransport(new URL(`${baseUrl}/mcp`));

        try {
            await client.connect(transport);
            const tools = await client.listTools();
            expect(tools.tools.map((tool) => tool.name)).toEqual(
                expect.arrayContaining([
                    "runtime_status",
                    "runtime_observe",
                    "runtime_key",
                    "play_interfaces",
                    "play_observe",
                    "play_step",
                    "play_step_status",
                ]),
            );

            const commandPromise = nextCommand(commandStream);
            const resultPromise = client.callTool({ name: "runtime_observe", arguments: {} });
            const command = await commandPromise;
            expect(command.request.type).toBe("runtime.observe");
            const completed = await fetch(`${baseUrl}/api/v1/editor/commands/result`, {
                method: "POST",
                headers: {
                    Authorization: `Bearer ${bootstrap.token}`,
                    "Content-Type": "application/json",
                },
                body: JSON.stringify({
                    id: command.id,
                    response: {
                        requestId: "editor-response",
                        ok: true,
                        value: {
                            mimeType: "image/png",
                            data: "cG5n",
                            width: 768,
                            height: 432,
                        },
                    },
                }),
            });
            expect(completed.status).toBe(200);
            expect(await resultPromise).toMatchObject({
                content: [
                    { type: "text", text: "Runtime viewport (768x432)." },
                    { type: "image", data: "cG5n", mimeType: "image/png" },
                ],
            });
        } finally {
            editorAbort.abort();
            await client.close().catch(() => undefined);
            await new Promise<void>((resolveClose) => host.server.close(() => resolveClose()));
            host.server.closeAllConnections();
        }
    });
});
