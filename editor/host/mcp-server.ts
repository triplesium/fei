import type { IncomingMessage, ServerResponse } from "node:http";
import { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import { StreamableHTTPServerTransport } from "@modelcontextprotocol/sdk/server/streamableHttp.js";
import { fromJSONSchema } from "zod/v4";
import { EditorCommandRelay } from "./editor-command-relay.js";
import { editorToolDefinitions } from "./editor-tool-catalog.js";

function createMcpServer(relay: EditorCommandRelay): McpServer {
    const server = new McpServer({ name: "entisium-editor", version: "0.1.0" });
    for (const definition of editorToolDefinitions) {
        if (
            !definition.name ||
            !definition.label ||
            !definition.description ||
            !definition.inputSchema ||
            !definition.request
        ) {
            continue;
        }
        const request = definition.request;
        server.registerTool(
            definition.name,
            {
                title: definition.label,
                description: definition.description,
                inputSchema: fromJSONSchema(definition.inputSchema),
                annotations: {
                    readOnlyHint: definition.readOnly ?? false,
                    destructiveHint: definition.command === "project.write",
                },
            },
            async (parameters) => {
                try {
                    const response = await relay.invoke(request(parameters));
                    if (!response.ok) {
                        return {
                            isError: true,
                            content: [
                                {
                                    type: "text" as const,
                                    text: response.error?.message ?? `${definition.command} failed`,
                                },
                            ],
                        };
                    }
                    return {
                        content: [
                            {
                                type: "text" as const,
                                text: JSON.stringify(response.value ?? null),
                            },
                        ],
                    };
                } catch (error) {
                    return {
                        isError: true,
                        content: [
                            {
                                type: "text" as const,
                                text: error instanceof Error ? error.message : String(error),
                            },
                        ],
                    };
                }
            },
        );
    }
    return server;
}

export async function handleMcpRequest(
    request: IncomingMessage,
    response: ServerResponse,
    body: unknown,
    relay: EditorCommandRelay,
): Promise<void> {
    if (request.method !== "POST") {
        response.writeHead(405, {
            Allow: "POST",
            "Content-Type": "application/json; charset=utf-8",
        });
        response.end(
            JSON.stringify({
                jsonrpc: "2.0",
                error: { code: -32000, message: "Method not allowed." },
                id: null,
            }),
        );
        return;
    }

    const server = createMcpServer(relay);
    const transport = new StreamableHTTPServerTransport({
        sessionIdGenerator: undefined,
        enableJsonResponse: true,
    });
    try {
        await server.connect(transport);
        await transport.handleRequest(request, response, body);
    } finally {
        await transport.close();
        await server.close();
    }
}
