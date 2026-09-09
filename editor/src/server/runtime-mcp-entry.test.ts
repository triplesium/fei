import { fileURLToPath } from "node:url";
import { Client } from "@modelcontextprotocol/sdk/client/index.js";
import { StdioClientTransport } from "@modelcontextprotocol/sdk/client/stdio.js";
import { expect, it } from "vitest";

it("keeps the old Editor MCP entrypoint as a shared-package compatibility shim", async () => {
    const client = new Client({ name: "legacy-entry-test", version: "1" });
    const transport = new StdioClientTransport({
        command: process.execPath,
        args: ["--import", "tsx", fileURLToPath(new URL("../../host/runtime-mcp-main.ts", import.meta.url))],
        stderr: "pipe",
    });
    try {
        await client.connect(transport);
        const { tools } = await client.listTools();
        expect(tools.map((tool) => tool.name)).toContain("runtime_play");
        expect(tools.map((tool) => tool.name)).toContain("play_step");
    } finally { await client.close(); }
}, 15_000);
