import { resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { StdioServerTransport } from "@modelcontextprotocol/sdk/server/stdio.js";
import { NativeRuntime } from "./native-runtime.js";
import { createRuntimeMcp } from "./runtime-mcp.js";

const root = fileURLToPath(new URL("../../", import.meta.url));
const runtime = new NativeRuntime(resolve(process.env.ETS_RUNTIME_HOST_PATH ?? resolve(
    root, "build", process.platform === "win32" ? "windows" : process.platform, process.arch,
    "debug", `entisium-runtime-host${process.platform === "win32" ? ".exe" : ""}`,
)));
const server = createRuntimeMcp(runtime);
let shuttingDown = false;
async function shutdown() {
    if (shuttingDown) return;
    shuttingDown = true;
    try { await runtime.stop(); await server.close(); }
    catch (error) { console.error(error); process.exitCode = 1; }
}
process.once("SIGINT", () => { void shutdown(); });
process.once("SIGTERM", () => { void shutdown(); });
process.stdin.once("end", () => { void shutdown(); });
server.server.onclose = () => { void shutdown(); };
await server.connect(new StdioServerTransport());
